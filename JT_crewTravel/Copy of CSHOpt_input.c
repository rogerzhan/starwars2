#include "os_config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <search.h>
#include <string.h>
#include <math.h>
#include "runway.h"
#include "datetime.h"
#include "my_mysql.h"
#include "params.h"
#include "split.h"
#include "bintree.h"
#include "stack.h"
#include "queue.h"
#include "mgets.h"
#include "strings.h"
#include "logMsg.h"
#include "airportLatLon.h"
#include "CSHOpt_struct.h"
#include "CSHOpt_readInput.h"
#include "pair.h"
#include "doFlightCalc.h"
#include "localTime.h"
#include "CSHOpt_define.h"
#include "CSHOpt_output.h"
#include "common.h"
#include "srchAndRpl.h"
#include "myoracle.h"


#define BONUS 0.0 // if we decide to assign a value to this we will make this a parameter file value.

char *bintreeRet[] = { "EmptyTree", "ExactMatchFound", "NotFoundReturnedNextItem", "GreaterThanLastItem", (char *) 0 };
#define DEBUGGING 1
///////////////////////////////////////////////////////////
// for crew routines
///////////////////////////////////////////////////////////
#ifdef MS_C_COMPILER
	static DateTime DateOnly = 0XFFFFFFFF00000000I64;
#else
	static DateTime DateOnly = 0XFFFFFFFF00000000LL;
#endif
///////////////////////////////////////////////////////////
//#define BeginningOfDayInGMT(x) ((x & DateOnly) | locFromGMTinMinutes * 60 * 1000)
//#define EndOfDayInGMT(x) (((((x & DateOnly) >> 32) + 1) << 32) | locFromGMTinMinutes * 60 * 1000)
#define BeginningOfDayInGMT(x) (x & DateOnly)
#define EndOfDayInGMT(x) ((((x & DateOnly) >> 32) + 1) << 32) //We already ajusted all time in GMT; 10/05/07 RLZ
#define OnSSday(dt_gmt,ssday) ((dt_gmt >= BeginningOfDayInGMT(ssday)) && (dt_gmt < EndOfDayInGMT(ssday)))
/*
#define SSdayInRange(dt_gmt_begin, ssday, dt_gmt_end) \
	( \
	 (BeginningOfDayInGMT(ssday) >= dt_gmt_begin && BeginningOfDayInGMT(ssday) < dt_gmt_end) || \
	 (EndOfDayInGMT(ssday) >= dt_gmt_begin && EndOfDayInGMT(ssday) < dt_gmt_end) \
	)
	*/

#define SSdayInRange(dt_gmt_begin, ssday, dt_gmt_end) \
( \
	(ssday <= dt_gmt_end && AddDays(1,ssday) >= dt_gmt_begin)  \
)

#define gmtDayInSSdayRange(ssday1, dt_gmt, ssday2) ((dt_gmt >= BeginningOfDayInGMT(ssday1)) && (dt_gmt <= EndOfDayInGMT(ssday2)))
#define gmtDayInSSdayRange1(ssday1, dt_gmt, ssday2) ( ssday1 <= AddDays(1,  dt_gmt) && ssday2 >=  dt_gmt)


#define AddDays(days,dt) (((((dt & DateOnly) >> 32) + days) << 32) | (dt & ~DateOnly))
//#define gmtDayToSSday(dt_gmt) ((BeginningOfDayInGMT(dt_gmt) > dt_gmt) ? AddDays(-1,(dt_gmt & DateOnly)) : \
//	(EndOfDayInGMT(dt_gmt) < dt_gmt) ? AddDays(1,(dt_gmt & DateOnly)) : (dt_gmt & DateOnly))
#define Max(a, b)  (((a) > (b)) ? (a) : (b))
#define Min(a, b)  (((a) < (b)) ? (a) : (b))

#define MAX_PLANES 200

/************************************************************************************************
*	Function	compareMaintLegSchedOut						Date last modified:  04/08/08 ANG	*
*	Purpose:	Used in qsort to sort maintenance list by sched out.							*
************************************************************************************************/
static int compareMaintLegSchedOut (const MaintenanceRecord *a, const MaintenanceRecord *b)
{
	return (int) (a->startTm - b->startTm);
}
/************************************************************************************************/

extern FILE *logFile;
extern int verbose;
extern int verbose1;
extern Moisture wetDry; // see runway.h. this is set with parameter type_3_exclusions
extern time_t run_time_t; // run time in GMT as time_t
extern DateTime dt_run_time_GMT; // run time in dt_ format in GMT
extern DateTime dt_run_time_LOC; // run time in dt_ format in local time zone
extern int locFromGMTinMinutes;  // number of minutes between gmt and local time
extern int oracleDirect; // read certain data directly from Oracle instead of from MySQL (snapshot must skip running SQL for tables read from directly from Oracle here)
extern int  local_scenarioid;
extern Warning_error_Entry *errorinfoList;
extern int errorNumber;

extern char *mysqlServer_oag;
extern char *mysqlUser_oag;
extern char *mysqlPW_oag;
extern char *database_oag;
extern int withOag;
extern ORACLE_SOCKET *orl_socket;
extern char *oracleServerSS;
extern char *oracleUserSS;
extern char *oraclePWSS;
extern char *oracleDBSS;
MY_CONNECTION *myconn = NULL;
MY_CONNECTION *remotemyconn = NULL;
MY_CONNECTION *myconn_oag = NULL;
OptParameters optParam;

int preNumCrewPair = 0;
int numCrewPairs = 0;
CrewPair *crewPairList;
MacInfo *macInfoList; //MAC - 09/02/08 ANG
QLIST *cpList;

PairConstraint *pairConstraintList = NULL;
int numPairConstraints = 0;
BINTREENODE *pcCrewIdToCrewIdRoot = NULL;
BINTREENODE *pcCrewIdToCategoryRoot = NULL;

int numLegs = 0;
Leg *legList = NULL;

int numFuelLegs = 0;
Leg *fuelLegList = NULL;

AircraftType *acTypeList = NULL; // sorted by sequence pos
int numAcTypes = 0;

Aircraft *acList = NULL;
int numAircraft = 0;
//McpAircraft *mcpAircraftList = NULL;// 03/12/08 ANG

BINTREENODE *intnlCertRoot = NULL;
int intnlCertCount = 0;
int intnlCertList[MAX_PLANES];

BINTREENODE *crewListRoot = NULL;
Crew *crewList = NULL;
int numCrew = 0;

MaintenanceRecord *maintList = NULL;
int numMaintenanceRecord = 0;
int numFakeMaintenanceRecord = 0; // 11/02/07 ANG

CrewEndTourRecord *crewEndTourList = NULL; // 11/12/07 ANG
int crewEndTourCount = 0; // 11/12/07 ANG

int aircraftCount = 0; // 11/12/07 ANG
int numMacInfo = 0; //MAC - 09/02/08 ANG

Demand *demandList;
int *recoveryDemandList;
int numDemand = 0;
int numDemandAllocd = 0; // readDemandList() allocates space for this many
int maxDemandID;
BINTREENODE *dmdXDemandIdRoot = NULL;
int numFakeDemand = 0;
int startFkDmdIdx = 0; //the start position in demandList for a contingency demand
double contingency_prob[ACTYPE_NUM] = {0.045, 0.12, 0.1, 0.15,0.17};
int hubsAptIDbyZones[6][4];
int maxDemandID; //Jintao contingency
Demand *testFlightList; //05/23/08 ANG
int countTestFlights = 0; //05/23/08 ANG

int numExclusion = 0; // total of numOtherExclusion and numCurfewExclusion
int numOtherExclusion = 0;
int numCurfewExclusion = 0;
Exclusion *exclusionList = NULL;
BINTREENODE *curfewexclusionRoot = NULL; // typeid and firstid are unique
BINTREENODE *exclusionRoot = NULL; // typeid, firstid and secondid are unique
Type_3_Exclusion_List *type3exclusionList = NULL;

//int numAirports = 9500; // got this from susan's code. Let's talk about this.
int numAirports = 0;
BINTREENODE *airportRoot = NULL;
BINTREENODE *rawAircraftRoot = NULL;
BINTREENODE *charterStatsRoot = NULL;
int charterStatsRecordCount = 0;

Owner *ownerList = NULL;
int numOwners = 0;

//Jintao's change
//Airport2 aptList[TOTAL_AIRPORTS_NUM + 1];
Airport2 *aptList; // Harry's change
ODEntry *oDTable; 
int numOD;

int peakDayCount = 0;
BINTREENODE *peakDayRoot = NULL;
int peakDayContractRateCount = 0;
BINTREENODE *peakDayContractRateRoot = NULL;

/*
leg *legList;

time_t firstMidnight;
int aptExcl[MAX_APTS][numAcTypes + 2]; //"columns" 0 and 1 are for curfew;  2 to numAcTypes are fleet-apt exclsns
int lastTripOfDay[MAX_WINDOW_DURATION-1];
*/

//////////////////////////////////////////////////////////////////////////////
// crew data
//////////////////////////////////////////////////////////////////////////////
PRE_Crew *preCrewList = NULL;
static int bwRecordCount = 0;
static int bwPairRecordCount = 0;
CrewData *bwCrewPairList = NULL;
static int csRecordCount = 0;
static int bwCrewIDrecordCount = 0;
static int ssRecordCount = 0;
static int uniqueDateCount = 0;
static DateTime pwStartDate = 0;
static DateTime pwEndDate = 0;
BINTREENODE *empnbrRoot = NULL;
BINTREENODE *crewidRoot = NULL;
BINTREENODE *empnbrStarttimeActypRegRoot = NULL;
BINTREENODE *crewidAcidStarttimeRoot = NULL;
BINTREENODE *acidStarttimeEndtimeCrewidRoot = NULL;
BINTREENODE *crewidStarttimeEndtimeAcidRoot=NULL;  //Jintao added-10/01/2007
BINTREENODE *aircraftCrewPairXrefRoot = NULL;
BINTREENODE *badgeDeptDateRoot = NULL;
BINTREENODE *dateListRoot = NULL;
BINTREENODE *cs_crew_dataRoot = NULL;
BINTREENODE *preCrewPairRoot = NULL;
BINTREENODE *crewPairRoot = NULL;
BINTREENODE *crewPairACRoot = NULL;
BINTREENODE *pxRoot = NULL;
BINTREENODE *pxPairRoot = NULL;
BINTREENODE *pxPairDupChkRoot = NULL;
BINTREENODE *legRoot = NULL;
BINTREENODE *travel_flightRoot = NULL;

// incremental revenue data
BINTREENODE *dmdKeyWdRoot = NULL;
BINTREENODE *ratiosRoot = NULL;
KW_Text kt[] = {
	{ KW_False, "False" },
	{ KW_Owner, "Owner" },
	{ KW_Demo, "Demo" },
	{ KW_Training, "Training" },
	{ KW_MX, "MX" },
	{ KW_Company_use, "Company use" },
	{ KW_Positioning, "Positioning" },
	{ KW_Upgrade_Approved, "Upgrade Approved" },
	{ KW_Downgrade_Approved, "Downgrade Approved" },
	{ KW_Upgrade_Req_200, "Upgrade Req. 200" },
	{ KW_Upgrade_Req_Bravo, "Upgrade Req. Bravo" },
	{ KW_Upgrade_Req_CJ1, "Upgrade Req. CJ1" },
	{ KW_Upgrade_Req_Excel, "Upgrade Req. Excel" },
	{ KW_Downgrade_Req_90, "Downgrade Req. 90" },
	{ KW_Downgrade_Req_200, "Downgrade Req. 200" },
	{ KW_Downgrade_Req_CJ1, "Downgrade Req. CJ1" },
	{ KW_Downgrade_Req_Bravo, "Downgrade Req. Bravo" },
	{ KW_Member_Use, "Member Use" },
	{ KW_FUEL_STOP, "FUEL STOP" },
	{ KW_Static_Demo, "Static Demo" },
	{ KW_Charity, "Charity" },
	{ KW_Customs_stop, "Customs stop" },
	{ KW_Delay_Arr_Wx, "Delay - Arr - Wx" },
	{ KW_Guaranteed_Upgrade, "Guaranteed Upgrade" },
	{ KW_Guaranteed_Downgrade, "Guaranteed Downgrade" },
	{ KW_Consecutive_Leg, "Consecutive Leg" },
	{ KW_NO_FUEL_STOP, "NO FUEL STOP" },
	{ KW_Delay_Arr_ATC, "Delay - Arr - ATC" },
	{ KW_Delay_Dep_Pax, "Delay - Dep - Pax" },
	{ KW_Delay_Dep_Wx, "Delay - Dep - Wx" },
	{ KW_Delay_Dep_ATC, "Delay - Dep - ATC" },
	{ KW_Delay_Dep_Mx, "Delay - Dep - Mx" },
	{ KW_Delay_Dep_FBO, "Delay - Dep - FBO" },
	{ KW_Delay_Arr_Pax, "Delay - Arr - Pax" },
	{ KW_Delay_Arr_Mx, "Delay - Arr - Mx" },
	{ KW_Delay_Arr_FBO, "Delay - Arr - FBO" },
	{ KW_Delay_Arr_Crew, "Delay - Arr - Crew" },
	{ KW_Delay_Dep_Crew, "Delay - Dep - Crew" },
	{ KW_Delay_Arr_Other, "Delay - Arr - Other" },
	{ KW_Delay_Dep_Other, "Delay - Dep - Other" },
	{ KW_Issue_Catering, "Issue - Catering" },
	{ KW_Issue_Ground, "Issue - Ground" },
	{ KW_Issue_FBO, "Issue - FBO" },
	{ KW_Issue_Accounting, "Issue - Accounting" },
	{ KW_Issue_Delayed_Flt, "Issue - Delayed Flt" },
	{ KW_Issue_Diversion, "Issue - Diversion" },
	{ KW_Issue_Mechanical, "Issue - Mechanical" },
	{ KW_Issue_Weather, "Issue - Weather" },
	{ KW_Issue_Pax_Complain, "Issue - Pax Complain" },
	{ KW_Issue_Charter, "Issue - Charter" },
	{ KW_Issue_Credits_Comp, "Issue - Credits/Comp" },
	{ KW_Issue_Booking_Err, "Issue - Booking Err" },
	{ KW_Issue_MP_Conflict, "Issue - MP Conflict" },
	{ KW_Issue_Customs, "Issue - Customs" },
	{ KW_Issue_Chartr_Avoid, "Issue - Chartr Avoid" },
	{ KW_Issue_A_C_Cond, "Issue - A/C Cond" },
	{ KW_Issue_Late_Pax, "Issue - Late Pax" },
	{ KW_Issue_ATC, "Issue - ATC" },
	{ KW_Issue_Late_pos_leg, "Issue - Late pos leg" },
	{ KW_Issue_Scheduling, "Issue - Scheduling" },
	{ KW_CTR_Sov_Guar, "CTR - Sov Guar" }, // what does this mean?
	{ KW_Upgrade_Req_Sov, "Upgrade Req. Sov" },
	{ KW_Fuel_Stop_Enroute, "Fuel Stop - Enroute" },
	{ KW_Fuel_Stop_Removed, "Fuel Stop - Removed" },
	{ KW_PPP, "PPP" },
	{ KW_Issue_Schdl_Devia, "Issue - Schdl Devia" },
	{ KW_Issue_Tail_Nbr_Chg, "Issue - Tail # Chg" },
	{ KW_Issue_Mx_no_Impact, "Issue - Mx no Impact" },
	{ KW_Caribbean_Express, "Caribbean Express" },
	{ KW_Issue_Vector_move, "Issue - Vector move" },
	{ KW_Peak_Day_Adj, "Peak Day Adj." },
	{ KW_Value_Plus, "Value Plus" },
	{ KW_Upgrade_Req_CJ3, "Upgrade Req. CJ3" },
	{ KW_Downgrade_Req_CJ3, "Downgrade Req. CJ3" },
	{ KW_Marketing, "Marketing" },
	{ KW_Downgrade_Req_Excel, "Downgrade Req. Excel" },
	{ KW_Ops_Reviewed, "Ops Reviewed" },
	{ KW_Chtr_Avd_Time_Accept, "Chtr Avd/Time Accept" },
	{ KW_Chtr_Avd_Time_Denied, "Chtr Avd/Time Denied" },
	{ KW_Chtr_Avd_A_C_Accept, "Chtr Avd-A/C-Accept" },
	{ KW_Chtr_Avd_A_C_Deny, "Chtr Avd-A/C-Deny" },
	{ KW_end_of_list, (char *) 0 }
};

SP_Text spt[] = {
	{ 3, "CJ1" },
	{ 4, "Bravo" },
	{ 5, "CJ3" },
	{ 6, "Excel" },
	{ 7, "Sovereign" },
	{ 0, (char *) 0 }
};

AC_Convert acv[] = {
	{ 3,  6,  KW_Upgrade_Req_CJ1,   KW_Downgrade_Req_CJ1,   0, 1 },  // CJ1
	{ 4,  5,  KW_Upgrade_Req_Bravo, KW_Downgrade_Req_Bravo, 1, 2 },  // Bravo
	{ 5, 54,  KW_Upgrade_Req_CJ3,   KW_Downgrade_Req_CJ3,   2, 5 },  // CJ3
	{ 6, 11,  KW_Upgrade_Req_Excel, KW_Downgrade_Req_Excel, 3, 3 },  // Excel
	{ 7, 52,  KW_Upgrade_Req_Sov,   KW_False,               4, 4 },  // Sovereign
	{ 0,  0,  0,                    0,                      0, 0 }
};
// end incremental revenue data

DateTime *dateList = NULL;
static char *tabmsg = "\
In the following table a row preceded by an asterisk indicates that the 'zpostdesc' field\n\
did not indicate Training, Training Travel or Flight Duty Officer and there was no\n\
crewassignment record in Bitwise assigning the individual to an aircraft for the given day.\n\
\n\
A row preceded by an exclamation point indicates that a pilot scheduled as a SIC\n\
in Schedule Soft was assigned as a PIC in Bitwise.";
//////////////////////////////////////////////////////////////////////////////
// end crew data. begin crew functions
//////////////////////////////////////////////////////////////////////////////
static int readCrewList(MY_CONNECTION *myconn);
static CrewID *textToCrewID(MYSQL_ROW row);
static CrewData *textToCrewData(MYSQL_ROW row);
static CS_CrewData *textToCsCrewData(MYSQL_ROW row);
static int empnbrStarttimeActypRegCmp(void *a1, void *b1);
static int cs_crew_data_crewidScheduledOnStarttimeCmp(void *a1, void *b1);
static int crewidAcidStarttimeCmp(void *a1, void *b1);
static void showCrewData(CrewData *cdPtr);
static void showCrewDataHeader(char *msg);
static void showCrewDataFooter(void);
static void showCsCrewData(CS_CrewData *cdPtr);
static void showCsCrewDataHeader(char *msg);
static void showCsCrewDataFooter(void);
//static void showCsCrewDataPreCrewHeader(char *msg);
//static void showCsCrewDataPreCrew(PRE_Crew *preCrewPtr);
//static void showCsCrewDataPreCrewFooter(char *msg);
static SS_CrewData *textToSS_CrewData(char **ptr);
static int badgeDeptDateCmp(void *a1, void *b1);
static void showSS_CrewData(SS_CrewData *ssCdPtr);
static void showSS_CrewDataHeader(char *msg);
static void showSS_CrewDataFooter(void);
static void showCombined_CrewData(SS_CrewData *ssCdPtr);
static void showCombined_CrewDataHeader(char *caption);
static void showCombined_CrewDataFooter(char *msg);
static void showCombined_CrewDataPreCrewHeader(char *caption);
//static void showCombined_CrewDataPreCrewFooter(char *msg);
static void showCombined_CrewDataPreCrew(PRE_Crew *preCrewPtr);
static void showCombined_CrewDataPreCrew_OnOff(PRE_Crew *preCrewPtr);
static void showCrewDataNotInSsHeader(char *msg);
static void showCrewDataNotInSsFooter(void);
static void showCrewDataNotInSs(CrewData *cdPtr, DateTime ssdt);
static int empnbrCmp(void *a1, void *b1);
static int crewidCmp(void *a1, void *b1);
static int charterStatsCmp(void *a1, void *b1);
static int crewListCrewidCmp(void *a1, void *b1);
static int bwWorkCmp(void *a1, void *b1);
static int dateListCmp(void *a1, void *b1);
static int travelFlightCmp(void *a1, void *b1);
static SS_CrewData *crewDataToSS_CrewData(SS_CrewData *ssCdPtr0, CrewData *cdPtr);
static int getAcTypeID(char *arg);
static int analysePositions(PRE_Crew *crewPtr);
static int analyseAircraftType(PRE_Crew *crewPtr);
static int cantFly(char *str);
static char * returnUpper(char *s);
static int readCrewIDS(MY_CONNECTION *myconn);
static int readLegData(MY_CONNECTION *myconn);
static int readFuelLegData(MY_CONNECTION *myconn);
static int readBwCrewData(MY_CONNECTION *myconn);
static int readCsCrewData(MY_CONNECTION *myconn);
static int readCsCrewDataFromOracle(void);
static int readCsCrewDataFromOracleDB(ORACLE_SOCKET *orl_socket);
static int readSsCrewData(MY_CONNECTION *myconn);
static int readSsCrewDataFromOracle(void);
static int readSsCrewDataFromOracleDB(ORACLE_SOCKET *orl_socket);
static void ssCrewDataProcess(void);
static int timeZoneAdjByApt(int AirportID, DateTime gmt);
static int readRecoveryDemandList(int* recoveryDemandNum); //read recovery demand list;
static int readCsTraveldata(MY_CONNECTION *myconn); 
static int readCsTraveldataFromOracleDB(ORACLE_SOCKET *orl_socket_cstrl);
void writeToIncrementalRevenueTable(MY_CONNECTION *myconn, int scenarioid); //03/30/09 ANG

//////////////////////////////////////////////////////////////////////////////
// end crew functions
//////////////////////////////////////////////////////////////////////////////

static int readIntnlCert(MY_CONNECTION *myconn);
// static int readIntnlCertFromOracle(void);
static int readContingencyFkDemand(MY_CONNECTION *myconn);
void  getctgncyincev(int actypeidx, int contingencyidx, int dmdnum, double *incRev);
static void readHubsbyZones(void);
static void addFkDemand(int dmdnum, int maxfkdmdnum, int day_division, DateTime timestart, DateTime timeend, int actypeidx, int zoneid, int fkdmdduration, int *totalfkdmdadded);

static int readOptParams(void);
static int readACTypeList(void);
static int readACList(MY_CONNECTION *myconn);
static int readMacInfo(MY_CONNECTION *myconn); //MAC - 08/29/08 ANG
static int readMacInfoFromOracleDB(ORACLE_SOCKET *orl_socket); //MAC - 09/03/08 ANG
static int readMaintList(MY_CONNECTION *myconn);
static int readDemandList(MY_CONNECTION *myconn);
static int readExclusionList(MY_CONNECTION *myconn);
static int getAirports(MY_CONNECTION *myconn);
static int airportCompare(void *a, void *b);
static int rawAircraftCompare(void *a, void *b);
static int demandIdCompare(void *a, void *b);
static int exclusionCompare(void *a, void *b);
static int curfewexclusionCompare(void *a, void *b);
static RawAircraftData *textToRawAircraftData(MYSQL_ROW row);
static void showRawAircraftDataHeader(void);
static void showRawAircraftDataFooter(void);
static void showRawAircraftData(RawAircraftData *radPtr);
static void showTravelLeg(CsTravelData *cstrlPtr);//RLZ
static void showAcListEntry(int aircraftID);
//static int getDutySoFar00(PRE_Crew *preCrewPtr);
static int getDutySoFar01(PRE_Crew *preCrewPtr);  // A replacement for static int getDutySoFar00
static int getDutySoFarWithCTC(PRE_Crew *preCrewPtr); // with CTC data integrated
static int getDutySoFarWithCTC01(PRE_Crew *preCrewPtr); // with CTC data integrated -RLZ AND JTO 09/18/08
static int getDutySoFarAlt(PRE_Crew *preCrewPtr); // Get earlier availDT if possible - 10/29/07 ANG
static int addFakeRecords(PRE_Crew *preCrewPtr);  // To bring pilots home - 11/02/07 ANG
static void printFakeRecords(void);				  // To bring pilots home - 11/02/07 ANG
static int validateCrewAircraftAssociation(char *empnbr, DateTime outtime, int aircraftid);
static int validateCrewAircraftAssociationMX(char *empnbr, DateTime outtime, DateTime intime, int aircraftid, char *rowType);
static int validateCrewAircraftAssociationFlightLeg(char *empnbr, DateTime outtime, DateTime intime, int aircraftid, char *rowType);
static int hasAlreadyFlown(PRE_Crew *preCrewPtr);
static int flownAircraftCompare(void *a, void *b);
static void getAcFlownList(PRE_Crew *preCrewPtr);
static void getLastActivityLeg(PRE_Crew *preCrewPtr);// Jintao's change 10/03/2007
static int starttimeCrewidCmp(void *a1, void *b1);
static int preCrewPairCmp(void *a1, void *b1);
static int crewPairACCmp(void *a1, void *b1);
static int crewPairCmp(void *a1, void *b1);
static int pxCmp(void *a1, void *b1);
static int pxPairCmp(void *a1, void *b1);
static int hasAlreadyFlownThisPlane(int crewid, int aircraftid, DateTime pairStartTm);
//New functions for crew process
static int crewidStarttimeEndtimeAcidCmp(void *a1, void *b1);
static void getAdjCrewAssg(PRE_Crew *precrewptr);
static int getCrewBlockTime(PRE_Crew *preCrewPtr);
static int getBaseToCsiMapping(int aircraftID); // 11/21/07 ANG

static int pairHasAlreadyFlownThisPlane(const CrewPairX *cpx, int aircraftID);  //Roger
static int checkAircraftList(int *list, int *count, int acid, int max);
static LEG_DATA *textToLegData(MYSQL_ROW row);
static int legCmp(void *a, void *b);
static Post getPostFromPostID(int lpostid);
static int getCategoryID(Post post, int position);
static void showCrewHeader(void);
static void showCrewFooter(void);
static void showCrew(Crew *cPtr);
static void getCrewCategoryID(PRE_Crew *preCrewPtr);
static Post catBitToPost(unsigned catbit);
static void textToPairConstraint(MYSQL_ROW row, PrePairConstraint *ppcPtr);
static int pcCrewIdToCrewIdCmp(void *a1, void *b1);
static int pcCrewIdToCategoryCmp(void *a1, void *b1);
static void displayPreCrewPair(PrePairConstraint *ppcPtr);
static int readCharterStats(MY_CONNECTION *myconn);
static int readPairConstraints(MY_CONNECTION *myconn);
static int getLostPlanes(MY_CONNECTION *myconn);
static DateTime getScheduledOnOff(PRE_Crew *preCrewPtr, DateTime dateToCheck, int onTime, CS_CrewData **cs_cdPPtr);
static int acidStarttimeEndtimeCrewidCmp(void *a1, void *b1);
static int aircraftCrewPairXrefCmp(void *a1, void *b1);
static int pxPairDupChkCmp(void *a1, void *b1);
static void getTourStartEnd(PRE_Crew *preCrewPtr); 
static void getCrewPairStartEnd(CrewPair *cpPtr); //, CrewPairX *pxPtr0);  //RLZ, 10/15/2007

static int readBwCrewPairData(MY_CONNECTION *myconn);
static char *hexToString(char *hex, char *outbuf, int maxout);

// incremental revenue functions
static void readUpgradeDowngrade(MY_CONNECTION *myconn);
static void getUpgradeDowngradeData(MY_CONNECTION *myconn);
static void getUpgradeDowngradeDataFromOracle(void);
static void getUpgradeDowngradeDataFromOracleDB(ORACLE_SOCKET *orl_socket);
static int demandidcmp(void *a1, void *b1);
static int contractidcmp(void *a1, void *b1);
static double getStandardRevenue(int contractid, int ac_type);
static int ac_conv(Cnv_Type in, int in_value, Cnv_Type out);
static double round(double x);

static KeyWord downGradeReqKW_List[] = { KW_Downgrade_Req_90, KW_Downgrade_Req_200, KW_Downgrade_Req_CJ1, KW_Downgrade_Req_Bravo, 
	KW_Downgrade_Req_CJ3, KW_Downgrade_Req_Excel, KW_False };

static KeyWord upGradeReqKW_List[] = { KW_Upgrade_Req_200, KW_Upgrade_Req_Bravo, KW_Upgrade_Req_CJ1, KW_Upgrade_Req_Excel,
	KW_Upgrade_Req_Sov, KW_Upgrade_Req_CJ3, KW_False };

static char *getKW_text(KeyWord kw);
static char *getsp_text(int sp);

static int kwExists(KeyWord kw, QLIST *q); // returns true if keyword in list, false otherwise

static int upgradeDowngradeKeywords(QLIST *q); // returns true if there are upgrade/downgrade req/approval/guaranteed keywords
static int upgradeKeywords(QLIST *q); // returns true if there are upgrade req/approval/guaranteed keywords
static int downgradeKeywords(QLIST *q); // returns true if there are downgrade req/approval/guaranteed keywords

static int kwDowngrade(DemandInfo *diPtr); // returns true if smaller sequence_Position is scheduled than the contract specifies.
                                  // Will return true if smaller sequence_position is scheduled even in the absense of downgrade request,
				  // downgrade approved, or guar. downgrade keywords.

static int kwUprade(DemandInfo *diPtr); // returns true if larger sequence_Position is scheduled than the contract specifies.
                                  // Will return true if larger sequence_position is scheduled even in the absense of upgrade request,
				  // upgrade approved, or guar. upgrade keywords.

static KeyWord kwDowngradeRequest(QLIST *q); // returns KW_False if false or, if true, returns the KeyWord value for whichever downgrade was requested
static KeyWord kwUpgradeRequest(QLIST *q); // returns KW_False if false or, if true, returns the KeyWord value for whichever upgrade was requested
static int kwUpgrade(DemandInfo *diPtr);
// end incremental revenue functions

static int readPeakDays(MY_CONNECTION *myconn);
//static int readPeakDaysFromOracle(void);
static int readPeakDaysContractRates(MY_CONNECTION *myconn);
static int readPeakDaysContractRatesFromOracle(void);
static int readPeakDaysContractRatesFromOracleDB(ORACLE_SOCKET *orl_socket);
static int peakDayContractRatesCompare(void *a, void *b);
static int peakDayCompare(void *a, void *b);
static void getPeakDayAdjustment(Demand *dPtr);
static int integerCmp(void *a, void *b);

//  Jintao's change
static int readAptList(MY_CONNECTION *myconn);
static void readOagODTable(MY_CONNECTION *myconn);
//

#ifdef DEBUGGING
static void displayResults(MYSQL_RES *res, MYSQL_FIELD *colInfo); //debug
#endif // DEBUGGING

/********************************************************************************
*	Function   readInputData              Date last modified:   /  /06 SWO		*
*	Purpose:  Read input data from mySQL db.									*  															
********************************************************************************/
int readInputData(char *host, char *user, char *password, char *database)
{
	extern void readTimeZone(MY_CONNECTION *myconn);
	extern void readTimeZoneFromOracle();

	//Connect to MySQL db
	if(! myconn) {
		myconn = myDoConnect(host, user, password ? password : "", database, 3306, (char *) 0, CLIENT_FOUND_ROWS);
		if(myconn->my_errno) 
		{
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
			writeWarningData(myconn); exit(1);
		}
	}

	if(!myconn_oag) {
		myconn_oag = myDoConnect(mysqlServer_oag, mysqlUser_oag, mysqlPW_oag ? mysqlPW_oag : "", database_oag, 3306, (char *) 0, CLIENT_FOUND_ROWS);
		if(myconn_oag->my_errno) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__, myconn_oag->my_errno, myconn_oag->my_error_msg);
			writeWarningData(myconn); exit(1);
		}
	}

	fflush(logFile);

    initAirportTables(myconn);	
	if(!(aptList = calloc(TOTAL_AIRPORTS_NUM + 1,sizeof(Airport2)))) {
		logMsg(logFile,"%s Line %d: Out of memory.\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	//exit(0); // debug
    //if(withOag==1)
	readAptList(myconn_oag);// Jintao's change

	if(oracleDirect==1)
	  readTimeZoneFromOracle();
	else if(oracleDirect==2)
	  readTimeZoneFromOracleDB(orl_socket);
	else
      readTimeZone(myconn);

	//fprintf(stderr,"%s Line %d, optParam.windowStart=%u\n", __FILE__,__LINE__,optParam.windowStart); //debug
	//read parameters
	readOptParams();
	//fprintf(stderr,"%s Line %d, optParam.windowStart=%u\n", __FILE__,__LINE__,optParam.windowStart); //debug

	//read input data
	readACTypeList();

//	if(oracleDirect)
//		readIntnlCertFromOracle();
//	else
		readIntnlCert(myconn);
		
	//MAC - 08/29/08 ANG		
	if (oracleDirect != 0){
        if(orl_socket == NULL) {
		  logMsg(logFile,"%s Line %d: db connection error\n", __FILE__,__LINE__);
		  exit(1);
	    }
		readMacInfoFromOracleDB(orl_socket);
	}
	else
	   readMacInfo(myconn);

	readACList(myconn);
	readMaintList(myconn);

//	if(oracleDirect)
//		readPeakDaysFromOracle();
//	else
		readPeakDays(myconn);

	if(oracleDirect == 1)
	   readPeakDaysContractRatesFromOracle();
	else if(oracleDirect == 2)	 
	   readPeakDaysContractRatesFromOracleDB(orl_socket);
	else
	   readPeakDaysContractRates(myconn);


	readDemandList(myconn);

	getRunwayData();
	getAirports(myconn);
	readExclusionList(myconn);
	readCrewIDS(myconn);
	//readBwCrewPairData(myconn); // Don't need this any more for hasFlownFirst. RLZ 10/18/07
	readBwCrewData(myconn);

	if(oracleDirect == 1)
		readCsCrewDataFromOracle();
	else if(oracleDirect == 2)
		readCsCrewDataFromOracleDB(orl_socket);
	else
		readCsCrewData(myconn);

	if(oracleDirect == 1)
		readSsCrewDataFromOracle();
	else if(oracleDirect == 2)
	{   ORACLE_SOCKET *orl_socket_ss;
		orl_socket_ss = Orlconnection_init_conn(oracleServerSS, oracleUserSS, oraclePWSS, oracleDBSS);
        if(orl_socket_ss == NULL) {
		  logMsg(logFile,"%s Line %d: db connection error\n", __FILE__,__LINE__);
		  exit(1);
	    }
		readSsCrewDataFromOracleDB(orl_socket_ss);
	    if(Orlconnection_close(orl_socket_ss)) {
		  logMsg(logFile,"%s Line %d: Ssoft database connection can't be close\n", __FILE__,__LINE__);
		  exit(1);
	   }
	}
	else
		readSsCrewData(myconn);

	ssCrewDataProcess();

	readLegData(myconn);
	//readFuelLegData(myconn);

	readCharterStats(myconn);

	if(optParam.withCTC){
		if (oracleDirect != 0){
			if(orl_socket == NULL) {
			logMsg(logFile,"%s Line %d: db connection error\n", __FILE__,__LINE__);
			exit(1);
			}
			readCsTraveldataFromOracleDB(orl_socket);
		}
		else
		readCsTraveldata(myconn);
	}
	    

	//fprintf(stderr,"%s Line %d, optParam.windowStart=%u\n", __FILE__,__LINE__,optParam.windowStart); //debug	
	readCrewList(myconn);
	//fprintf(stderr,"%s Line %d, optParam.windowStart=%u\n", __FILE__,__LINE__,optParam.windowStart); //debug	
	readPairConstraints(myconn);

	readUpgradeDowngrade(myconn);

	writeToIncrementalRevenueTable(myconn, local_scenarioid);

	//12/17/08 ANG - Move stats collection for SCHEDULE to the beginning of the process to minimize changes in the database
	//if(optParam.runOptStatsByDay == 1 && optParam.planningWindowDuration == 3) runScheduleStatisticsByDay(myconn, local_scenarioid); 
	if(optParam.runOptStatsByDay == 1 && optParam.planningWindowDuration > 1) runScheduleStatisticsByDay(myconn, local_scenarioid); 

	if (optParam.runType < 2&& withOag==1) 
	(void) readOagODTable(myconn_oag);   //Jintao's changes-last change
		
//	mysql_close(myconn->mysock);
	if(oracleDirect ==2){
	  if(Orlconnection_close(orl_socket)) {
		  logMsg(logFile,"%s Line %d: Ssoft database connection can't be close\n", __FILE__,__LINE__);
		  exit(1);
	   }
	}

	return 0;
}

/*
 * To get Airport-Curfew Start type 1 exclusion:
 *      input: typeID = 1, firstID = airportID
 *
 * To get Airport-Curfew End type 2 exclusion:
 *      input: typeID = 2, firstID = airportID
 *
 * To check for the existence of an AircraftType-Airport type 3 exclusion:
 *      input: typeID = 3, firstID = aircraftTypeID, secondID = airportID
 *
 * To check for the existence of a demand-aircraft type 4 exclusion:
 *      input: typeID = 4, firstID = demandID, secondID = aircraftID
 *
 * Output:
 *     If exclusion found
 *         ret == 1
 *         expptr = pointer to Exclusion object
 *     Else
 *         ret == 0
 *         expptr = null pointer
 *
 */
int
getExclusion(int typeID, int firstID, int secondID, Exclusion **expptr)
{
	BINTREENODE *tmp;
	Exclusion exBuf;

	switch(typeID) {
	case Excln_Airport_CurfewStart:
	case Excln_Airport_CurfewEnd:
		exBuf.typeID = typeID;
		exBuf.firstID = firstID;
		tmp = TreeSearch(curfewexclusionRoot, &exBuf, curfewexclusionCompare);
		if(!tmp) {
			*expptr = (Exclusion *) 0;
			return(0);
		}
		*expptr = getTreeObject(tmp);
		return(1);
	case Excln_AircraftType_Airport:
	case Excln_Demand_Aircraft:
		exBuf.typeID = typeID;
		exBuf.firstID = firstID;
		exBuf.secondID = secondID;
		tmp = TreeSearch(exclusionRoot, &exBuf, exclusionCompare);
		if(!tmp) {
			*expptr = (Exclusion *) 0;
			return(0);
		}
		*expptr = getTreeObject(tmp);
		return(1);
	default:
		return(0);
	}
}

int
getAircraftTypeAirportExclusions(int aircraftTypeID, int **airportIDList)
{
	int x;
	Type_3_Exclusion_List *t3ptr;

	*airportIDList = (int *) 0;

	for(x = 0, t3ptr = type3exclusionList; x < numAcTypes; x++, t3ptr++) {
		if(t3ptr->aircraftTypeID == aircraftTypeID)
			break;
	}
	if(x == numAcTypes)
		return(0); // we didn't have that type id in the list

	if(t3ptr->count) {
		*airportIDList = t3ptr->airportIDptr;
		return(1);
	}
	else
		return(0);
}

/********************************************************************************
*	Function   readOptParams              Date last modified:   /  /06 SWO
*	Purpose:  Read in optParams from mySQL db.
********************************************************************************/
static int readOptParams(void)
{
	extern char *getCmdLineVar(char *varName);
	char *paramVal;
	char tbuf[1024];
	char scratch[1024];
	int errNbr, x, wc;
	char *wptrs[128];


	if(verbose) logMsg(logFile,"** Parameters read in by readOptParams():\n");

//	/* required: "autoLockTm" */
//	if(!(paramVal = getParamValue("autoLockTm"))) {
//		logMsg(logFile,"Required parameter \"autoLockTm\" missing from parameter file.\n");
//		writeWarningData(myconn); exit(1);
//	}
//	optParam.autoLockTm = atoi(paramVal);
//	if(verbose) fprintf(logFile,"%27s = %5d\n", "autoLockTm", optParam.autoLockTm);
//

	/* required: "estimateCrewLoc" - 01/17/08 RLZ*/

	if(verbose) fprintf(logFile,"%27s = %5d\n", "oracleDirect", oracleDirect);

	if(verbose) fprintf(logFile,"%27s = %5d\n", "paramsOracle", optParam.paramsOracle);


	if(!(paramVal = getCmdLineVar("estimateCrewLoc"))) {
		if(!(paramVal = getParamValue("estimateCrewLoc"))) {
			logMsg(logFile,"Required parameter \"estimateCrewLoc\" missing from parameter file.\n");
			writeWarningData(myconn); 
			exit(1);
		}
	}
	optParam.estimateCrewLoc = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "estimateCrewLoc", optParam.estimateCrewLoc);

	/* required: "autoFlyHome" - 11/08/07 ANG*/
	if(!(paramVal = getCmdLineVar("autoFlyHome"))) {
	  if(!(paramVal = getParamValue("autoFlyHome"))) {
		logMsg(logFile,"Required parameter \"autoFlyHome\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.autoFlyHome = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "autoFlyHome", optParam.autoFlyHome);

	/* required: "beyondPlanningWindowBenefit" */
	if(!(paramVal = getParamValue("beyondPlanningWindowBenefit"))) {
		logMsg(logFile,"Required parameter \"beyondPlanningWindowBenefit\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.beyondPlanningWindowBenefit = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "beyondPlanningWindowBenefit", optParam.beyondPlanningWindowBenefit);

//	changeNextPenalty; //Susan had comment in optimParam.txt saying to remove this

	/* required: "changeNxtDayPenalty" */
	if(!(paramVal = getCmdLineVar("changeNxtDayPenalty"))) {
		if(!(paramVal = getParamValue("changeNxtDayPenalty"))) {
			logMsg(logFile,"Required parameter \"changeNxtDayPenalty\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	optParam.changeNxtDayPenalty = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "changeNxtDayPenalty", optParam.changeNxtDayPenalty);

	/* required: "changeTodayPenalty" */
	if(!(paramVal = getCmdLineVar("changeTodayPenalty"))) {
		if(!(paramVal = getParamValue("changeTodayPenalty"))) {
			logMsg(logFile,"Required parameter \"changeTodayPenalty\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	optParam.changeTodayPenalty = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "changeTodayPenalty", optParam.changeTodayPenalty);

	/* required: "crewDutyAfterCommFlt" */
	if(!(paramVal = getCmdLineVar("crewDutyAfterCommFlt"))) {
		if(!(paramVal = getParamValue("crewDutyAfterCommFlt"))) {
			logMsg(logFile,"Required parameter \"crewDutyAfterCommFlt\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	optParam.crewDutyAfterCommFlt = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "crewDutyAfterCommFlt", optParam.crewDutyAfterCommFlt);

	/* required: "crewPairBonus" */
	if(!(paramVal = getParamValue("crewPairBonus"))) {
		logMsg(logFile,"Required parameter \"crewPairBonus\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.crewPairBonus = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "crewPairBonus", optParam.crewPairBonus);

	/* required: "cutoffForFinalRepo" */
	if(!(paramVal = getParamValue("cutoffForFinalRepo"))) {
		logMsg(logFile,"Required parameter \"cutoffForFinalRepo\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.cutoffForFinalRepo = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "cutoffForFinalRepo", optParam.cutoffForFinalRepo);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//      This parameter now read in in main()
//	/* required: "dayEndTime" */
//	if(!(paramVal = getParamValue("dayEndTime"))) {
//		logMsg(logFile,"Required parameter \"dayEndTime\" missing from parameter file.\n");
//		writeWarningData(myconn); exit(1);
//	}
//	optParam.dayEndTime = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "dayEndTime", optParam.dayEndTime);
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/* required: "downgrPairPriority1" - 01/21/09 ANG */
	if(!(paramVal = getCmdLineVar("downgrPairPriority1"))) {
		if(!(paramVal = getParamValue("downgrPairPriority1"))) {
			logMsg(logFile,"Required parameter \"downgrPairPriority1\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	optParam.downgrPairPriority1 = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "downgrPairPriority1", optParam.downgrPairPriority1);

	/* required: "dutyStartFromMidnight" - 10/30/07 ANG */
	if(!(paramVal = getCmdLineVar("dutyStartFromMidnight"))) {
		if(!(paramVal = getParamValue("dutyStartFromMidnight"))) {
			logMsg(logFile,"Required parameter \"dutyStartFromMidnight\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	optParam.dutyStartFromMidnight = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "dutyStartFromMidnight", optParam.dutyStartFromMidnight);

	/* required: "earlyCostPH" */
	if(!(paramVal = getParamValue("earlyCostPH"))) {
		logMsg(logFile,"Required parameter \"earlyCostPH\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.earlyCostPH = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "earlyCostPH", optParam.earlyCostPH);

	/* required: "earlierAvailability" - 10/30/07 ANG */
	if(!(paramVal = getCmdLineVar("earlierAvailability"))) {
		if(!(paramVal = getParamValue("earlierAvailability"))) {
			logMsg(logFile,"Required parameter \"earlierAvailability\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	optParam.earlierAvailability = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "earlierAvailability", optParam.earlierAvailability);


	/* required: "fboTransitTm" */
	if(!(paramVal = getParamValue("fboTransitTm"))) {
		logMsg(logFile,"Required parameter \"fboTransitTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.fboTransitTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "fboTransitTm", optParam.fboTransitTm);

	/* required: "finalPostFltTm" */
	if(!(paramVal = getCmdLineVar("finalPostFltTm"))) {
	  if(!(paramVal = getParamValue("finalPostFltTm"))) {
		logMsg(logFile,"Required parameter \"finalPostFltTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.finalPostFltTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "finalPostFltTm", optParam.finalPostFltTm);

	/* required: "firstPreFltTm" */
	if(!(paramVal = getCmdLineVar("firstPreFltTm"))) {
	  if(!(paramVal = getParamValue("firstPreFltTm"))) {
		logMsg(logFile,"Required parameter \"firstPreFltTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.firstPreFltTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "firstPreFltTm", optParam.firstPreFltTm);

	/* required: "fuelStopTm" */
	if(!(paramVal = getParamValue("fuelStopTm"))) {
		logMsg(logFile,"Required parameter \"fuelStopTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.fuelStopTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "fuelStopTm", optParam.fuelStopTm);

	/* required: "inclContingencyDmd" - 09/08/08 ANG*/
	if(!(paramVal = getCmdLineVar("inclContingencyDmd"))) {
	  if(!(paramVal = getParamValue("inclContingencyDmd"))) {
		logMsg(logFile,"Required parameter \"inclContingencyDmd\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.inclContingencyDmd = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "inclContingencyDmd", optParam.inclContingencyDmd);

	/* required: "inclInfeasExgSol" */
	if(!(paramVal = getParamValue("inclInfeasExgSol"))) {
		logMsg(logFile,"Required parameter \"inclInfeasExgSol\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.inclInfeasExgSol = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "inclInfeasExgSol", optParam.inclInfeasExgSol);

	/* required: "includeLockedUmlg" - 02/12/08 ANG*/
	if(!(paramVal = getCmdLineVar("includeLockedUmlg"))) {
	  if(!(paramVal = getParamValue("includeLockedUmlg"))) {
		logMsg(logFile,"Required parameter \"includeLockedUmlg\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.includeLockedUmlg = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "includeLockedUmlg", optParam.includeLockedUmlg);

	/* required: "ignoreSwapLoc" */
	if(!(paramVal = getCmdLineVar("ignoreSwapLoc"))) {
	  if(!(paramVal = getParamValue("ignoreSwapLoc"))) {
		logMsg(logFile,"Required parameter \"ignoreSwapLoc\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.ignoreSwapLoc = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "ignoreSwapLoc", optParam.ignoreSwapLoc);

	/* required: "ignoreMacOS" */
	if(!(paramVal = getCmdLineVar("ignoreMacOS"))) {
	  if(!(paramVal = getParamValue("ignoreMacOS"))) {
		logMsg(logFile,"Required parameter \"ignoreMacOS\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.ignoreMacOS = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "ignoreMacOS", optParam.ignoreMacOS);

	/* required: "ignoreMacOSMX" */
	if(!(paramVal = getCmdLineVar("ignoreMacOSMX"))) {
	  if(!(paramVal = getParamValue("ignoreMacOSMX"))) {
		logMsg(logFile,"Required parameter \"ignoreMacOSMX\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.ignoreMacOSMX = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "ignoreMacOSMX", optParam.ignoreMacOSMX);

	/* required: "ignoreMac" - 01/05/09 ANG*/
	if(!(paramVal = getCmdLineVar("ignoreMac"))) {
	  if(!(paramVal = getParamValue("ignoreMac"))) {
		logMsg(logFile,"Required parameter \"ignoreMac\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.ignoreMac = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "ignoreMac", optParam.ignoreMac);

	/* required: "ignoreMacDmd" - 01/05/09 ANG*/
	if(!(paramVal = getCmdLineVar("ignoreMacDmd"))) {
	  if(!(paramVal = getParamValue("ignoreMacDmd"))) {
		logMsg(logFile,"Required parameter \"ignoreMacDmd\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.ignoreMacDmd = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "ignoreMacDmd", optParam.ignoreMacDmd);

	/* required: "ignoreAllOS" */
	if(!(paramVal = getCmdLineVar("ignoreAllOS"))) {
	  if(!(paramVal = getParamValue("ignoreAllOS"))) {
		logMsg(logFile,"Required parameter \"ignoreAllOS\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.ignoreAllOS = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "ignoreAllOS", optParam.ignoreAllOS);

	/* required: "lateCostPH" */
	if(!(paramVal = getParamValue("lateCostPH"))) {
		logMsg(logFile,"Required parameter \"lateCostPH\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.lateCostPH = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "lateCostPH", optParam.lateCostPH);

	/* required: "macBonus" - MAC - 09/05/08 ANG*/
	if(!(paramVal = getCmdLineVar("macBonus"))) {
	  if(!(paramVal = getParamValue("macBonus"))) {
		logMsg(logFile,"Required parameter \"macBonus\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.macBonus = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "macBonus", optParam.macBonus);


	/* required: "updateforSimu" - Write proposed_managedleg into managedleg table - 09/05/08 ANG*/
	if(!(paramVal = getCmdLineVar("updateforSimu"))) {
	  if(!(paramVal = getParamValue("updateforSimu"))) {
		logMsg(logFile,"Required parameter \"updateforSimu\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.updateforSimu = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "updateforSimu", optParam.updateforSimu);






	/* required: "macPenalty" - MAC - 09/05/08 ANG*/
	if(!(paramVal = getCmdLineVar("macPenalty"))) {
	  if(!(paramVal = getParamValue("macPenalty"))) {
		logMsg(logFile,"Required parameter \"macPenalty\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.macPenalty = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "macPenalty", optParam.macPenalty);

	/* required: "maintTmForReassign" */
	if(!(paramVal = getParamValue("maintTmForReassign"))) {
		logMsg(logFile,"Required parameter \"maintTmForReassign\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.maintTmForReassign = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "maintTmForReassign", optParam.maintTmForReassign);

	/* required: "maintTurnTime" */
	if(!(paramVal = getCmdLineVar("maintTurnTime"))) {
	   if(!(paramVal = getParamValue("maintTurnTime"))) {
		logMsg(logFile,"Required parameter \"maintTurnTime\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	   }
	}
	optParam.maintTurnTime = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "maintTurnTime", optParam.maintTurnTime);

	/* required: "maxDutyTm" */
	if(!(paramVal = getParamValue("maxDutyTm"))) {
		logMsg(logFile,"Required parameter \"maxDutyTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.maxDutyTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "maxDutyTm", optParam.maxDutyTm);

	/* required: "maxFakeMaintRec" - 11/08/07 ANG*/
	if(!(paramVal = getParamValue("maxFakeMaintRec"))) {
		logMsg(logFile,"Required parameter \"maxFakeMaintRec\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.maxFakeMaintRec = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "maxFakeMaintRec", optParam.maxFakeMaintRec);

	/* required: "maxFlightTm" */
	if(!(paramVal = getParamValue("maxFlightTm"))) {
		logMsg(logFile,"Required parameter \"maxFlightTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.maxFlightTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "maxFlightTm", optParam.maxFlightTm);

	/* required: "maxRepoTm" */
	if(!(paramVal = getParamValue("maxRepoTm"))) {
		logMsg(logFile,"Required parameter \"maxRepoTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.maxRepoTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "maxRepoTm", optParam.maxRepoTm);

	/* required: "maxUpgrades" */
	/*
	if(!(paramVal = getCmdLineVar("maxUpgrades"))) {
		if(!(paramVal = getParamValue("maxUpgrades"))) {
			logMsg(logFile,"Required parameter \"maxUpgrades\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	optParam.maxUpgrades = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "maxUpgrades", optParam.maxUpgrades);
    */
	/* required: "minCharterTime" */
	if(!(paramVal = getParamValue("minCharterTime"))) {
		logMsg(logFile,"Required parameter \"minCharterTime\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.minCharterTime = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "minCharterTime", optParam.minCharterTime);

	/* required: "minRestTm" */
	if(!(paramVal = getParamValue("minRestTm"))) {
		logMsg(logFile,"Required parameter \"minRestTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.minRestTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "minRestTm", optParam.minRestTm);

	/* required: "minTimeToDuty" */
	//if(!(paramVal = getParamValue("minTimeToDuty"))) {
	//	logMsg(logFile,"Required parameter \"minTimeToDuty\" missing from parameter file.\n");
	//	writeWarningData(myconn); exit(1);
	//}
	//optParam.minTimeToDuty = atoi(paramVal);
	//if(verbose) fprintf(logFile,"%27s = %5d\n", "minTimeToDuty", optParam.minTimeToDuty);

	/* required: "minTimeToNotify" */
	if(!(paramVal = getCmdLineVar("minTimeToNotify"))) {
		if(!(paramVal = getParamValue("minTimeToNotify"))) {
			logMsg(logFile,"Required parameter \"minTimeToNotify\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}

	optParam.minTimeToNotify = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "minTimeToNotify", optParam.minTimeToNotify);

	/* required: "minTmToTourStart" */
	if(!(paramVal = getCmdLineVar("minTmToTourStart"))) {
		if(!(paramVal = getParamValue("minTmToTourStart"))) {
			logMsg(logFile,"Required parameter \"minTmToTourStart\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}

	optParam.minTmToTourStart = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "minTmToTourStart", optParam.minTmToTourStart);

	/* required: "crewTourStartInMin" */
	if(!(paramVal = getParamValue("crewTourStartInMin"))) {
		logMsg(logFile,"Required parameter \"crewTourStartInMin\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.crewTourStartInMin = atoi(paramVal);

	if(verbose) fprintf(logFile,"%27s = %5d\n", "crewTourStartInMin", optParam.crewTourStartInMin);

	/* required: "maxCrewExtension" */
	if(!(paramVal = getCmdLineVar("maxCrewExtension"))) {
		if(!(paramVal = getParamValue("maxCrewExtension"))) {
			logMsg(logFile,"Required parameter \"maxCrewExtension\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	optParam.maxCrewExtension = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "maxCrewExtension", optParam.maxCrewExtension);

	/* required: "numToursPerCrewPerItn" */
	if(!(paramVal = getParamValue("numToursPerCrewPerItn"))) {
		logMsg(logFile,"Required parameter \"numToursPerCrewPerItn\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.numToursPerCrewPerItn = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "numToursPerCrewPerItn", optParam.numToursPerCrewPerItn);

	/* required: "overTimeCost" */
	if(!(paramVal = getParamValue("overTimeCost"))) {
		logMsg(logFile,"Required parameter \"overTimeCost\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.overTimeCost = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "overTimeCost", optParam.overTimeCost);

	/* required: "overTimeHalfCost" */
	if(!(paramVal = getParamValue("overTimeHalfCost"))) {
		logMsg(logFile,"Required parameter \"overTimeHalfCost\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.overTimeHalfCost = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "overTimeHalfCost", optParam.overTimeHalfCost);

//	/* required: "peakEnd" */
//	if(!(paramVal = getParamValue("peakEnd"))) {
//		logMsg(logFile,"Required parameter \"peakEnd\" missing from parameter file.\n");
//		writeWarningData(myconn); exit(1);
//	}
//	optParam.peakEnd = atoi(paramVal);
//	if(verbose) fprintf(logFile,"%27s = %5d\n", "peakEnd", optParam.peakEnd);

	/* required: "pairingLevel" */
	if(!(paramVal = getCmdLineVar("pairingLevel"))) {
		if(!(paramVal = getParamValue("pairingLevel"))) {
			logMsg(logFile,"Required parameter \"pairingLevel\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	optParam.pairingLevel = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "pairingLevel", optParam.pairingLevel);

	/* required: "peakDayLevel_1_Adj" */
	if(!(paramVal = getCmdLineVar("peakDayLevel_1_Adj"))) {
	  if(!(paramVal = getParamValue("peakDayLevel_1_Adj"))) {
		logMsg(logFile,"Required parameter \"peakDayLevel_1_Adj\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.peakDayLevel_1_Adj = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "peakDayLevel_1_Adj", optParam.peakDayLevel_1_Adj);

	/* required: "peakDuration" */
	if(!(paramVal = getParamValue("peakDuration"))) {
		logMsg(logFile,"Required parameter \"peakDuration\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.peakDuration = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "peakDuration", optParam.peakDuration);

	/* required: "peakGMTDuration" */
	if(!(paramVal = getParamValue("peakGMTDuration"))) {
		logMsg(logFile,"Required parameter \"peakGMTDuration\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.peakGMTDuration = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "peakGMTDuration", optParam.peakGMTDuration);

	/* required: "peakGMTStart" */
	if(!(paramVal = getParamValue("peakGMTStart"))) {
		logMsg(logFile,"Required parameter \"peakGMTStart\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.peakGMTStart = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "peakGMTStart", optParam.peakGMTStart);

	/* required: "peakOverlapBenefit" */
	if(!(paramVal = getParamValue("peakOverlapBenefit"))) {
		logMsg(logFile,"Required parameter \"peakOverlapBenefit\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.peakOverlapBenefit = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "peakOverlapBenefit", optParam.peakOverlapBenefit);

	/* required: "peakStart" */
	if(!(paramVal = getParamValue("peakStart"))) {
		logMsg(logFile,"Required parameter \"peakStart\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.peakStart = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "peakStart", optParam.peakStart);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//      These parameters now read in in main()
//	/* required: "planningWindowDuration" */
//	if(!(paramVal = getParamValue("planningWindowDuration"))) {
//		logMsg(logFile,"Required parameter \"planningWindowDuration\" missing from parameter file.\n");
//		writeWarningData(myconn); exit(1);
//	}
//	optParam.planningWindowDuration = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "planningWindowDuration", optParam.planningWindowDuration);
//
//	/* required: "planningWindowStart" */
//	if(!(paramVal = getParamValue("planningWindowStart"))) {
//		logMsg(logFile,"Required parameter \"planningWindowStart\" missing from parameter file.\n");
//		writeWarningData(myconn); exit(1);
//	}
//	optParam.planningWindowStart = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "planningWindowStart", optParam.planningWindowStart);
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	if(verbose) fprintf(logFile,"%27s = %5d\n", "planningFakeRuntime", optParam.planningFakeRuntime); // 02/19/08 ANG

	/* required: "postArrivalTime" */
	if(!(paramVal = getCmdLineVar("postArrivalTime"))) {
	  if(!(paramVal = getParamValue("postArrivalTime"))) {
		logMsg(logFile,"Required parameter \"postArrivalTime\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.postArrivalTime = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "postArrivalTime", optParam.postArrivalTime);

	/* required: "postFlightTm" */
	if(!(paramVal = getCmdLineVar("postFlightTm"))) {
	  if(!(paramVal = getParamValue("postFlightTm"))) {
		logMsg(logFile,"Required parameter \"postFlightTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.postFlightTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "postFlightTm", optParam.postFlightTm);

	/* required: "preBoardTime" */
	if(!(paramVal = getCmdLineVar("preBoardTime"))) {
     	if(!(paramVal = getParamValue("preBoardTime"))) {
		  logMsg(logFile,"Required parameter \"preBoardTime\" missing from parameter file.\n");
		  writeWarningData(myconn); exit(1);
	    }
	}
	optParam.preBoardTime = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "preBoardTime", optParam.preBoardTime);


	/* required: "preFlightTm" */
	if(!(paramVal = getCmdLineVar("preFlightTm"))) {
	  if(!(paramVal = getParamValue("preFlightTm"))) {
		logMsg(logFile,"Required parameter \"preFlightTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.preFlightTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "preFlightTm", optParam.preFlightTm);

	/* required: "priorityBenefit" */
	if(!(paramVal = getParamValue("priorityBenefit"))) {
		logMsg(logFile,"Required parameter \"priorityBenefit\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	strcpy(tbuf, paramVal);
	wc = split(tbuf," \t", wptrs);
	if(wc != 4) {
		logMsg(logFile,"expected 4 \"priorityBenefit\" values, got %d instead.\n", wc);
		writeWarningData(myconn); exit(1);
	}
	for(x = 0; x < wc; ++x) {
		optParam.priorityBenefit[x] = atoi(wptrs[x]);
		if(verbose) {
			sprintf(scratch,"priorityBenefit[%d]", x);
			fprintf(logFile,"%27s = %5d\n", scratch, optParam.priorityBenefit[x]);
		}
	}

	/* required: "prohibitStealingPlanes" */
	if(!(paramVal = getCmdLineVar("prohibitStealingPlanes"))) {
		if(!(paramVal = getParamValue("prohibitStealingPlanes"))) {
			logMsg(logFile,"Required parameter \"prohibitStealingPlanes\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	optParam.prohibitStealingPlanes = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "prohibitStealingPlanes", optParam.prohibitStealingPlanes);

	/* required: "restToDutyTm" */
	if(!(paramVal = getParamValue("restToDutyTm"))) {
		logMsg(logFile,"Required parameter \"restToDutyTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.restToDutyTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "restToDutyTm", optParam.restToDutyTm);

	if(verbose) fprintf(logFile,"%27s = %5d\n", "runType", optParam.runType);

	/* required: "runOptStats" - 02/12/08 ANG*/
	if(!(paramVal = getCmdLineVar("runOptStats"))) {
	  if(!(paramVal = getParamValue("runOptStats"))) {
		logMsg(logFile,"Required parameter \"runOptStats\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.runOptStats = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "runOptStats", optParam.runOptStats);

	/* required: "writeSimulationData" - 03/11/08 ANG*/
	//if(!(paramVal = getCmdLineVar("writeSimulationData"))) {
	//  if(!(paramVal = getParamValue("writeSimulationData"))) {
	//	logMsg(logFile,"Required parameter \"writeSimulationData\" missing from parameter file.\n");
	//	writeWarningData(myconn); exit(1);
	//  }
	//}
	//optParam.writeSimulationData = atoi(paramVal);
	//if(verbose) fprintf(logFile,"%27s = %5d\n", "writeSimulationData", optParam.writeSimulationData);

	/* required: "runOptStatsByDay" - 02/12/08 ANG*/
	if(!(paramVal = getCmdLineVar("runOptStatsByDay"))) {
	  if(!(paramVal = getParamValue("runOptStatsByDay"))) {
		logMsg(logFile,"Required parameter \"runOptStatsByDay\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.runOptStatsByDay = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "runOptStatsByDay", optParam.runOptStatsByDay);

	/* required: "runWithContingency" - 05/13/08 Jintao*/
	if(!(paramVal = getCmdLineVar("runWithContingency"))) {
	  if(!(paramVal = getParamValue("runWithContingency"))) {
		logMsg(logFile,"Required parameter \"runWithContingency\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.runWithContingency = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "runWithContingency", optParam.runWithContingency);

	/* required: "taxiInTm" */
	if(!(paramVal = getParamValue("taxiInTm"))) {
		logMsg(logFile,"Required parameter \"taxiInTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.taxiInTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "taxiInTm", optParam.taxiInTm);

	/* required: "taxiOutTm" */
	if(!(paramVal = getParamValue("taxiOutTm"))) {
		logMsg(logFile,"Required parameter \"taxiOutTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.taxiOutTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "taxiOutTm", optParam.taxiOutTm);

	/* required: "ticketCostFixed" */
	if(!(paramVal = getParamValue("ticketCostFixed"))) {
		logMsg(logFile,"Required parameter \"ticketCostFixed\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.ticketCostFixed = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "ticketCostFixed", optParam.ticketCostFixed);

	/* required: "ticketCostVar" */
	if(!(paramVal = getParamValue("ticketCostVar"))) {
		logMsg(logFile,"Required parameter \"ticketCostVar\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.ticketCostVar = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "ticketCostVar", optParam.ticketCostVar);

	/* required: "travelcutoff" - 09/11/08 ANG*/
	if(verbose) fprintf(logFile,"%27s = %5d\n", "travelcutoff", optParam.travelcutoff);

	/* required: "turnTime" */
	if(!(paramVal = getCmdLineVar("turnTime"))) {
	  if(!(paramVal = getParamValue("turnTime"))) {
		logMsg(logFile,"Required parameter \"turnTime\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.turnTime = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "turnTime", optParam.turnTime);

	/* required: "uncovDemoPenalty" */
	if(!(paramVal = getCmdLineVar("uncovDemoPenalty"))) {
	  if(!(paramVal = getParamValue("uncovDemoPenalty"))) {
		logMsg(logFile,"Required parameter \"uncovDemoPenalty\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	   }
	}
	optParam.uncovDemoPenalty = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "uncovDemoPenalty", optParam.uncovDemoPenalty);

	/* required: "uncovMaintPenalty" */
	if(!(paramVal = getParamValue("uncovMaintPenalty"))) {
		logMsg(logFile,"Required parameter \"uncovMaintPenalty\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	optParam.uncovMaintPenalty = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "uncovMaintPenalty", optParam.uncovMaintPenalty);

	/* required: "uncovFlyHomePenalty - 11/06/07 ANG" */
	if(!(paramVal = getCmdLineVar("uncovFlyHomePenalty"))) {
	  if(!(paramVal = getParamValue("uncovFlyHomePenalty"))) {
		logMsg(logFile,"Required parameter \"uncovFlyHomePenalty\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.uncovFlyHomePenalty = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "uncovFlyHomePenalty", optParam.uncovFlyHomePenalty);

	/* required: "uncovFlyHomePenalty2 - 04/22/08 ANG" */
	if(!(paramVal = getCmdLineVar("uncovFlyHomePenalty2"))) {
	  if(!(paramVal = getParamValue("uncovFlyHomePenalty2"))) {
		logMsg(logFile,"Required parameter \"uncovFlyHomePenalty2\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.uncovFlyHomePenalty2 = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "uncovFlyHomePenalty2", optParam.uncovFlyHomePenalty2);

	/* required: "vectorWin" */
	if(!(paramVal = getCmdLineVar("vectorWin"))) {
	  if(!(paramVal = getParamValue("vectorWin"))) {
		logMsg(logFile,"Required parameter \"vectorWin\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.vectorWin = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "vectorWin", optParam.vectorWin);

	if(verbose)
		fprintf(logFile,"%27s = %s\n",
			"windowEnd",
			dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(optParam.windowEnd))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M"));

	if(verbose)
		fprintf(logFile,"%27s = %s\n",
			"windowStart",
			dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(optParam.windowStart))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M"));
    
	if(verbose) fprintf(logFile,"%27s = %5d\n", "withOag", optParam.withOag);

    /* required: "withCTC" */
    if(! (paramVal = getCmdLineVar("withCTC"))) {
		if(!(paramVal = getParamValue("withCTC"))) {
			fprintf(stderr,"Required parameter \"withCTC\" missing from parameter file.\n");
			exit(1);
		}
	}
	optParam.withCTC = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "withCTC", optParam.withCTC);

    /* required: "withMac" - MAC 09/23/08 ANG */
    if(! (paramVal = getCmdLineVar("withMac"))) {
		if(!(paramVal = getParamValue("withMac"))) {
			fprintf(stderr,"Required parameter \"withMac\" missing from parameter file.\n");
			exit(1);
		}
	}
	optParam.withMac = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "withMac", optParam.withMac);

//	/* required: "xsCharterPenalty" */
//	if(!(paramVal = getParamValue("xsCharterPenalty"))) {
//		logMsg(logFile,"Required parameter \"xsCharterPenalty\" missing from parameter file.\n");
//		writeWarningData(myconn); exit(1);
//	}
//	optParam.xsCharterPenalty = atof(paramVal);
//	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "xsCharterPenalty", optParam.xsCharterPenalty);

	/* required: "xsCharterPenalty" */
	if(!(paramVal = getParamValue("xsCharterPenalty"))) {
		logMsg(logFile,"Required parameter \"xsCharterPenalty\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	strcpy(tbuf, paramVal);
	wc = split(tbuf," \t", wptrs);
	if(wc != 3) {
		logMsg(logFile,"expected 3 \"xsCharterPenalty\" values, got %d instead.\n", wc);
		writeWarningData(myconn); exit(1);
	}
	for(x = 0; x < wc; ++x) {
		optParam.xsCharterPenalty[x] = atof(wptrs[x]);
		if(verbose) {
			sprintf(scratch,"xsCharterPenalty[%d]", x);
			fprintf(logFile,"%27s = %8.2f\n", scratch, optParam.xsCharterPenalty[x]);
		}
	}


	if(!(paramVal = getParamValue("dutyNodeAdjInterval"))) {
		logMsg(logFile,"Required parameter \"dutyNodeAdjInterval\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}	
	optParam.dutyNodeAdjInterval = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "dutyNodeAdjInterval", optParam.dutyNodeAdjInterval);

	if(!(paramVal = getParamValue("earlyCostPH_recovery"))) {
		logMsg(logFile,"Required parameter \"earlyCostPH_recovery\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}	
	optParam.earlyCostPH_recovery = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5.2f\n", "earlyCostPH_recovery", optParam.earlyCostPH_recovery);

	if(!(paramVal = getParamValue("lateCostPH_recovery"))) {
		logMsg(logFile,"Required parameter \"lateCostPH_recovery\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}	
	optParam.lateCostPH_recovery = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5.2f\n", "lateCostPH_recovery", optParam.lateCostPH_recovery);



	if(!(paramVal = getCmdLineVar("recoveryAdj_early"))) {
	  if(!(paramVal = getParamValue("recoveryAdj_early"))) {
		logMsg(logFile,"Required parameter \"recoveryAdj_early\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.recoveryAdj_early = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "recoveryAdj_early", optParam.recoveryAdj_early);




	if(!(paramVal = getCmdLineVar("recoveryAdj_late"))) {
	  if(!(paramVal = getParamValue("recoveryAdj_late"))) {
		logMsg(logFile,"Required parameter \"recoveryAdj_late\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.recoveryAdj_late = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "recoveryAdj_late", optParam.recoveryAdj_late);


	if(!(paramVal = getParamValue("downgradePenaltyRatio"))) {
		logMsg(logFile,"Required parameter \"downgradePenaltyRatio\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}

	optParam.downgradePenaltyRatio = atof(paramVal);
	if(verbose) fprintf(logFile,"%27s = %4.2f\n", "downgradePenaltyRatio", optParam.downgradePenaltyRatio);

	//Early duty rule.
	if(!(paramVal = getParamValue("cutoffForShortDuty"))) {
		logMsg(logFile,"Required parameter \"cutoffForShortDuty\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}

	optParam.cutoffForShortDuty = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "cutoffForShortDuty", optParam.cutoffForShortDuty);

	if(!(paramVal = getParamValue("shortDutyTm"))) {
		logMsg(logFile,"Required parameter \"shortDutyTm\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}

	optParam.shortDutyTm = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "shortDutyTm", optParam.shortDutyTm);

	if(!(paramVal = getParamValue("minRestTmLong"))) {
		logMsg(logFile,"Required parameter \"minRestTmLong\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}

	optParam.minRestTmLong = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "minRestTmLong", optParam.minRestTmLong);
// RLZ: lock for don't reassign the existing crew pairs
	if(!(paramVal = getCmdLineVar("exgCPACLock"))) {
	  if(!(paramVal = getParamValue("exgCPACLock"))) {
		logMsg(logFile,"Required parameter \"exgCPACLock\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.exgCPACLock = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "exgCPACLock", optParam.exgCPACLock);

	/* required: "exgCPACBonus" 02/19/09 ANG */
	if(!(paramVal = getCmdLineVar("exgCPACBonus"))) {
	  if(!(paramVal = getParamValue("exgCPACBonus"))) {
		logMsg(logFile,"Required parameter \"exgCPACBonus\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.exgCPACBonus = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %8.2f\n", "exgCPACBonus", optParam.exgCPACBonus);

	if(verbose) logMsg(logFile,"** End Parameters read in by readOptParams().\n\n"); //logMsg does fflush()
	return 0;
}

/********************************************************************************
* Function   readACTypeList              Date last modified:   /  /06 SWO
* Purpose:  Read in Aircraft Type List from mySQL db.	List is sorted by
* sequence position
********************************************************************************/
static int readACTypeList(void)
{
	int wc, x, wcUpgrades;
	char *wptrs[128],*wptrsUpgrades[128];
	char tbuf[1024],tbuf1[1024];
	char vbuf[128];
	char *paramVal;

	if(verbose) logMsg(logFile,"** Parameters read in by readACTypeList():\n");
	/* required: ac_type_names */
	if(!(paramVal = getParamValue("ac_type_names"))) {
		logMsg(logFile,"Required parameter \"ac_type_names\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	}
	if(verbose) logMsg(logFile,"ac_type_names = %s\n", paramVal);
	strcpy(tbuf, paramVal);
	wc = split(tbuf," \t", wptrs);
	acTypeList = (AircraftType *) calloc(wc, sizeof(AircraftType));
	if(! acTypeList) {
		logMsg(logFile,"Out of Memory in readACTypeList().\n");
		writeWarningData(myconn); exit(1);
	}

	// _ac_type_id, _seq_pos, _chrtr_cost, _oper_cost, _taxi_cost _std_rev, _mac_cost
	for(x = 0; x < wc; x++) {
		strcpy(vbuf, wptrs[x]);
		strcat(vbuf, "_ac_type_id");
		if(!(paramVal = getParamValue(vbuf))) {
			logMsg(logFile,"Required parameter \"%s\" missing from parameter file.\n", vbuf);
			writeWarningData(myconn); exit(1);
		}
		acTypeList[x].aircraftTypeID = atoi(paramVal);
	}


	for(x = 0; x < wc; x++) {
		strcpy(vbuf, wptrs[x]);
		strcat(vbuf, "_seq_pos");
		if(!(paramVal = getParamValue(vbuf))) {
			logMsg(logFile,"Required parameter \"%s\" missing from parameter file.\n", vbuf);
			writeWarningData(myconn); exit(1);
		}
		acTypeList[x].sequencePosn = atoi(paramVal);
	}

	for(x = 0; x < wc; x++) {
		strcpy(vbuf, wptrs[x]);
		strcat(vbuf, "_chrtr_cost");
		if(!(paramVal = getParamValue(vbuf))) {
			logMsg(logFile,"Required parameter \"%s\" missing from parameter file.\n", vbuf);
			writeWarningData(myconn); exit(1);
		}
		acTypeList[x].charterCost = atof(paramVal);
	}

	for(x = 0; x < wc; x++) {
		strcpy(vbuf, wptrs[x]);
		strcat(vbuf, "_oper_cost");
		if(!(paramVal = getParamValue(vbuf))) {
			logMsg(logFile,"Required parameter \"%s\" missing from parameter file.\n", vbuf);
			writeWarningData(myconn); exit(1);
		}
		acTypeList[x].operatingCost = atof(paramVal);
		//acTypeList[x].macOwnerCost = atof(paramVal);
	}

	for(x = 0; x < wc; x++) {
		strcpy(vbuf, wptrs[x]);
		strcat(vbuf, "_taxi_cost");
		if(!(paramVal = getParamValue(vbuf))) {
			logMsg(logFile,"Required parameter \"%s\" missing from parameter file.\n", vbuf);
			writeWarningData(myconn); exit(1);
		}
		acTypeList[x].taxiCost = atof(paramVal);
	}

	for(x = 0; x < wc; x++) {
		strcpy(vbuf, wptrs[x]);
		strcat(vbuf, "_std_rev");
		if(!(paramVal = getParamValue(vbuf))) {
			logMsg(logFile,"Required parameter \"%s\" missing from parameter file.\n", vbuf);
			writeWarningData(myconn); exit(1);
		}
		acTypeList[x].standardRevenue = atof(paramVal);
	}

	for(x = 0; x < wc; x++) {
		strcpy(vbuf, wptrs[x]);
		strcat(vbuf, "_maxUpgrades");
		if(!(paramVal = getParamValue(vbuf))) {
			logMsg(logFile,"Required parameter \"%s\" missing from parameter file.\n", vbuf);
			writeWarningData(myconn); exit(1);
		}
		acTypeList[x].maxUpgrades = atoi(paramVal);
	}

	if((paramVal = getCmdLineVar("maxUpgrades"))) {
		strcpy(tbuf1, paramVal);
		wcUpgrades = split(tbuf1," \t", wptrsUpgrades);
		if(wcUpgrades != 4) {
			logMsg(logFile,"expected 4 \"maxUpgrades \" values, got %d instead.\n", wcUpgrades);
			writeWarningData(myconn); exit(1);
		}
		for(x = 0; x < wcUpgrades; ++x) {
			acTypeList[x].maxUpgrades = atoi(wptrsUpgrades[x]);
		}
		acTypeList[wc-1].maxUpgrades = 0; //Highest fleet, no upgrades
	}

	for(x = 0; x < wc; x++) {
		strcpy(vbuf, wptrs[x]);
		strcat(vbuf, "_capacity");
		if(!(paramVal = getParamValue(vbuf))) {
			logMsg(logFile,"Required parameter \"%s\" missing from parameter file.\n", vbuf);
			writeWarningData(myconn); exit(1);
		}
		acTypeList[x].capacity = atoi(paramVal);
	}

	//MAC - 09/12/08 ANG
	for(x = 0; x < wc; x++) {
		strcpy(vbuf, wptrs[x]);
		strcat(vbuf, "_mac_cost");
		if(!(paramVal = getParamValue(vbuf))) {
			logMsg(logFile,"Required parameter \"%s\" missing from parameter file.\n", vbuf);
			writeWarningData(myconn); exit(1);
		}
		acTypeList[x].macOprCost = atof(paramVal) + optParam.macPenalty;
	}


	/* required: recovery option */
	if(!(paramVal = getCmdLineVar("downgradeRecovery"))) {
		if(!(paramVal = getParamValue("downgradeRecovery"))) {
			logMsg(logFile,"Required parameter \"downgradeRecovery\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	strcpy(tbuf1, paramVal);
	wcUpgrades = split(tbuf1," \t", wptrsUpgrades);
	if(wcUpgrades != 4) {
		logMsg(logFile,"expected 4 \"downgradeRecovery \" values, got %d instead.\n", wcUpgrades);
		writeWarningData(myconn); exit(1);
	}
	for(x = 0; x < wcUpgrades ; ++x) {
		acTypeList[x + 1].downgradeRecovery = atoi(wptrsUpgrades[x]);
	}
	acTypeList[0].downgradeRecovery = 0; //Lowest fleet, no downgrades

	if(!(paramVal = getCmdLineVar("upgradeRecovery"))) {
		if(!(paramVal = getParamValue("upgradeRecovery"))) {
			logMsg(logFile,"Required parameter \"upgradeRecovery\" missing from parameter file.\n");
			writeWarningData(myconn); exit(1);
		}
	}
	strcpy(tbuf1, paramVal);
	wcUpgrades = split(tbuf1," \t", wptrsUpgrades);
	if(wcUpgrades != 4) {
		logMsg(logFile,"expected 4 \"upgradeRecovery \" values, got %d instead.\n", wcUpgrades);
		writeWarningData(myconn); exit(1);
	}
	for(x = 0; x < wcUpgrades; ++x) {
		acTypeList[x].upgradeRecovery = atoi(wptrsUpgrades[x]);
	}
	acTypeList[wc - 1].upgradeRecovery = 0; //Highest fleet, no upgrades

	if(verbose) {
		fprintf(logFile,"+---------+------------+---------+------------+-----------+-----------+-----------+------------+------------+----------+---------+-----------+\n");
		fprintf(logFile,"| AC TYPE | AC TYPE ID | SEQ POS | CHRTR COST | OPER COST | MAC COST  | TAXI COST |    STD REV | MAXUPGRADE | CAPACITY | UPRCVRY | DOWNRCVRY |\n");
		fprintf(logFile,"+---------+------------+---------+------------+-----------+-----------+-----------+------------+------------+----------+---------+-----------+\n");
		for(x = 0; x < wc; x++) {
			fprintf(logFile,"| %7s | %10d | %7d | %10.2f | %9.2f | %9.2f | %9.2f | %10.2f | %10d | %8d | %7d | %9d |\n",
				wptrs[x], acTypeList[x].aircraftTypeID, acTypeList[x].sequencePosn,
				acTypeList[x].charterCost, acTypeList[x].operatingCost, acTypeList[x].macOprCost,
				acTypeList[x].taxiCost, acTypeList[x].standardRevenue, acTypeList[x].maxUpgrades, acTypeList[x].capacity, acTypeList[x].upgradeRecovery, acTypeList[x].downgradeRecovery); 
		}
		fprintf(logFile,"+---------+------------+---------+------------+-----------+-----------+-----------+------------+------------+----------+---------+-----------+\n");
	}
	numAcTypes = wc;
	
	if(verbose) logMsg(logFile,"** End Parameters read in by readACTypeList().\n\n"); //logMsg() does fflush()
	return 0;
}


/********************************************************************************
*	Function   getLostPlanes()
*	Purpose:   read in lostplanes
********************************************************************************/
typedef struct lostplanes {
	char *legtype;
	char *registration;
	int aircraftid;
	int leg_pos;
	int leg_ac_type;
	char *dmd_ac_type;
	int demandid;
	int leg_id;
	char *leg_outtime;
	char *dmd_outtime;
	char *leg_intime;
	char *dmd_intime;
	char *actualout;
	char *actualoff;
	char *actualin;
	char *actualon;
	char *shortname;
	int ownerid;
	int tripno;
	char *leg_outicao;
	char *dmd_outicao;
	char *leg_inicao;
	int leg_inaptid;
	int leg_outfboid;
	int dmd_outfboid;
	int leg_infboid;
	int dmd_infboid;
	int tzlegout;
	int tzdmdout;
	int tzlegin;
	int tzdmdin;
} LostPlanes;
static LostPlanes *lostPlanesList = NULL;
static int lostPlanesCount = 0;

typedef enum { LP_legtype = 0, LP_registration, LP_aircraftid, LP_leg_pos, LP_leg_ac_type, LP_dmd_ac_type, LP_demandid, LP_leg_id,
LP_leg_outtime, LP_dmd_outtime, LP_leg_intime, LP_dmd_intime, LP_actualout, LP_actualoff, LP_actualin, LP_actualon, LP_shortname,
LP_ownerid, LP_tripno, LP_leg_outicao, LP_dmd_outicao, LP_leg_inicao, LP_leg_inaptid, LP_leg_outfboid, LP_dmd_outfboid, LP_leg_infboid,
LP_dmd_infboid, LP_tzlegout, LP_tzdmdout, LP_tzlegin, LP_tzdmdin, LP_end_of_list = 255
} LP_columns;

static int
getLostPlanes(MY_CONNECTION *myconn)
{

	char *lostPlanesSQL = "select * from lostplanes";  
	MYSQL_RES *res;
	MYSQL_FIELD *cols;

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	LostPlanes *lpPtr;

	if(!myDoQuery(myconn, lostPlanesSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"getLostPlanes(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"getLostPlanes(): 0 rows returned.\n");
		return(0);
	}

	lostPlanesList = (LostPlanes *) calloc((size_t)(((size_t) rowCount) + 1), sizeof(LostPlanes));
	if(! lostPlanesList) {
		logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	for(rows = 0, lpPtr = lostPlanesList; rows < rowCount; rows++, lpPtr++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;

		if(!(lpPtr->legtype = strdup(row[LP_legtype]))) {
			logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(!(lpPtr->registration = strdup(row[LP_registration]))) {
			logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		lpPtr->aircraftid = row[LP_aircraftid] ? atoi(row[LP_aircraftid]) : 0;
		lpPtr->leg_pos = row[LP_leg_pos] ? atoi(row[LP_leg_pos]) : 0;
		lpPtr->leg_ac_type = row[LP_leg_ac_type] ? atoi(row[LP_leg_ac_type]) : 0;
		if(row[LP_dmd_ac_type]) {
			if(!(lpPtr->dmd_ac_type = strdup(row[LP_dmd_ac_type]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		lpPtr->demandid = row[LP_demandid] ? atoi(row[LP_demandid]) : 0;
		lpPtr->leg_id = row[LP_leg_id] ? atoi(row[LP_leg_id]) : 0;
		if(row[LP_leg_outtime]) {
			if(!(lpPtr->leg_outtime = strdup(row[LP_leg_outtime]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[LP_dmd_outtime]) {
			if(!(lpPtr->dmd_outtime = strdup(row[LP_dmd_outtime]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[LP_leg_intime]) {
			if(!(lpPtr->leg_intime = strdup(row[LP_leg_intime]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[LP_dmd_intime]) {
			if(!(lpPtr->dmd_intime = strdup(row[LP_dmd_intime]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[LP_actualout]) {
			if(!(lpPtr->actualout = strdup(row[LP_actualout]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[LP_actualoff]) {
			if(!(lpPtr->actualoff = strdup(row[LP_actualoff]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[LP_actualin]) {
			if(!(lpPtr->actualin = strdup(row[LP_actualin]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[LP_actualon]) {
			if(!(lpPtr->actualon = strdup(row[LP_actualon]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[LP_shortname]) {
			if(!(lpPtr->shortname = strdup(row[LP_shortname]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		lpPtr->ownerid = row[LP_ownerid] ? atoi(row[LP_ownerid]) : 0;
		lpPtr->tripno = row[LP_tripno] ? atoi(row[LP_tripno]) : 0;
		if(row[LP_leg_outicao]) {
			if(!(lpPtr->leg_outicao = strdup(row[LP_leg_outicao]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[LP_dmd_outicao]) {
			if(!(lpPtr->dmd_outicao = strdup(row[LP_dmd_outicao]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[LP_leg_inicao]) {
			if(!(lpPtr->leg_inicao = strdup(row[LP_leg_inicao]))) {
				logMsg(logFile,"%s Line %d, Out of Memory in getLostPlanes().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		lpPtr->leg_inaptid = row[LP_leg_inaptid] ? atoi(row[LP_leg_inaptid]) : 0;
		lpPtr->leg_outfboid = row[LP_leg_outfboid] ? atoi(row[LP_leg_outfboid]) : 0;
		lpPtr->dmd_outfboid = row[LP_dmd_outfboid] ? atoi(row[LP_dmd_outfboid]) : 0;
		lpPtr->leg_infboid = row[LP_leg_infboid] ? atoi(row[LP_leg_infboid]) : 0;
		lpPtr->dmd_infboid = row[LP_dmd_infboid] ? atoi(row[LP_dmd_infboid]) : 0;
		lpPtr->tzlegout = row[LP_tzlegout] ? atoi(row[LP_tzlegout]) : 0;
		lpPtr->tzdmdout = row[LP_tzdmdout] ? atoi(row[LP_tzdmdout]) : 0;
		lpPtr->tzlegin = row[LP_tzlegin] ? atoi(row[LP_tzlegin]) : 0;
		lpPtr->tzdmdin = row[LP_tzdmdin] ? atoi(row[LP_tzdmdin]) : 0;
		++lostPlanesCount;
	}

	if(verbose) {
		int ix;
		logMsg(logFile,"\nlost planes:\n");
		fprintf(logFile,"+--------------+--------------+------------+-------------+------------------+-------------+------------------+\n");
		fprintf(logFile,"| legtype      | registration | aircraftid | leg_outicao | leg_outtime      | leg_inicao  | leg_intime       |\n");
		fprintf(logFile,"+--------------+--------------+------------+-------------+------------------+-------------+------------------+\n");
		for(ix = 0, lpPtr = lostPlanesList; ix < lostPlanesCount; ix++, lpPtr++) {
			fprintf(logFile,"| %-12s | %-12s | %10d | %-11s | %16s | %-11s | %16s |\n",
				lpPtr->legtype,
				lpPtr->registration,
				lpPtr->aircraftid,
				lpPtr->leg_outicao,
				lpPtr->leg_outtime,
				lpPtr->leg_inicao,
				lpPtr->leg_intime);
		}
		fprintf(logFile,"+--------------+--------------+------------+-------------+------------------+-------------+------------------+\n");
		fflush(logFile);
	}

	// free mysql results
	mysql_free_result(res);
	return 0;

}
/********************************************************************************
 *	Function   readACList			     Date last modified:
 *	Purpose:  Read in Aircraft List from mySQL db.
 ********************************************************************************/
typedef enum {
	whereRowtype = 0, whereRec_id, whereAircraftid, whereRegistration, whereAc_type, whereDemandID, whereOutaptid, whereOuticao, whereInaptid,
	whereInicao, whereOutfboid, whereInfboid, whereRec_outtime, whereRec_intime, whereActualout, whereActualoff, whereActualon, whereActualin, whereSequenceposition,
	whereCrewnotified, where_end_of_list = 255
} whereSqlColumns;
static int
readACList(MY_CONNECTION *myconn)
{
	extern char *whereAreThePlanesSQL;
	LostPlanes *lpPtr;
	Aircraft *tmpACList;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;

	MYSQL_ROW row;
	MYSQL_ROW nextRow; // 02/28/08 ANG
	my_ulonglong rowCount, rows;
	Aircraft *tPtr, *tPtr2;
	DateTime dt, tdt;//, ndt;
	int errNbr = 0;
	int nbrUnqAc = 0; // number of unique aircraft in results
	int currentAircraftID = 0;
	int tempAircraftID = 0;
	int firstAircraftID = 0;
	int addTaxiInTm;
	int idx;
	char intime[32];
	char nextOutTime[32];// 02/28/08 ANG
	Airport *aptPtr, aptBuf;
	RawAircraftData *radPtr, *prevRadPtr, radBuf;
	RawAircraftData *radPtrPrevious;
	BINTREENODE *tmp, *oldTree;
	char tbuf[32];
	char writetodbstring1[200];
	int a, skip; //MAC - 09/02/08 ANG  
	int nbSkip = 0; // 11/04/08 ANG
	int tempACID = 0; //MAC - 01/05/09 ANG

	getLostPlanes(myconn);

	// experimental
	tdt = dt_time_tToDateTime(optParam.windowStart);
	// end experimental

	if(!myDoQuery(myconn, whereAreThePlanesSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readACList(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readACList(): 0 rows returned.\n");
		writeWarningData(myconn); exit(1);
	}
#ifdef DEBUGGING
	logMsg(logFile,"raw query results for whereAreThePlanesSQL:\n");
	if (verbose) displayResults(res, cols);
	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
#endif // DEBUGGING

	radPtrPrevious = (RawAircraftData *) calloc((size_t) 1, sizeof(RawAircraftData));

    radPtrPrevious->aircraftid = 0;

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		radPtr = textToRawAircraftData(row);
		if(! radPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readAcList().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if (radPtr->aircraftid == radPtrPrevious->aircraftid && radPtr->rec_outtime == radPtrPrevious->rec_outtime){
			logMsg(logFile,"%s Line %d, tail: %s, rowtype: %s, outtime: %s\n",
				__FILE__,__LINE__, radPtr->registration, radPtr->rowtype,
		    		(radPtr->rec_outtime) ? dt_DateTimeToDateTimeString(radPtr->rec_outtime,tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
            logMsg(logFile,"Possible Reason: duplicated records (same aircraftid and outtime). Record skipped!  \n");
			//Add output
			continue;
		}

		//START - Skipping all records for MAC - MAC - 01/05/09 ANG
		if(optParam.ignoreMac == 1){
			skip = 0;
			tempACID = (row[whereAircraftid]) ? atoi(row[whereAircraftid]) : 0;
			if(tempACID > 0){
				//search through M-aircraft list
				for(a = 0; a < numMacInfo; a++){
					if(atoi(row[whereAircraftid]) == macInfoList[a].aircraftID){
						//nbSkip++;
						skip = 1;
						//if (verbose) {
						//	fprintf(logFile, "Skipping OS record %d for aircraftID %d.\n", atoi(row[whereRec_id]), macInfoList[a].aircraftID);
						//}
						break;
					}
				}
				if (skip == 1)
					continue;
			}
		}
		//END - MAC - 01/05/09 ANG

		//START - Skipping OS - All or MAC - 11/04/08 ANG
		if(optParam.ignoreAllOS == 1){
			if (strcmp(row[whereRowtype],"ownersign") == 0){
				//nbSkip++;
				continue;
			}
		}
		if(optParam.ignoreMacOS == 1 && optParam.withMac == 1 && optParam.ignoreAllOS == 0){
			skip = 0;
			if(strcmp(row[whereRowtype],"ownersign") == 0){
				//search through M-aircraft list
				for(a = 0; a < numMacInfo; a++){
					if(atoi(row[whereAircraftid]) == macInfoList[a].aircraftID){
						//nbSkip++;
						skip = 1;
						//if (verbose) {
						//	fprintf(logFile, "Skipping OS record %d for aircraftID %d.\n", atoi(row[whereRec_id]), macInfoList[a].aircraftID);
						//}
						break;
					}
				}
				if (skip == 1)
					continue;
			}
		}
		//END - MAC - 11/04/08 ANG

		//START - Add condition to ignore Mac's OS and MX if started after windowStart - MAC - 01/07/09 ANG
		if(optParam.ignoreMacOSMX == 1 && optParam.ignoreMac == 0 && optParam.withMac == 1){
			skip = 0;
	        if(row[whereRec_outtime]) {
				dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[whereRec_outtime], NULL, &errNbr); //tdt: windowStart
			}
			if((strcmp(row[whereRowtype],"ownersign") == 0 || strcmp(row[whereRowtype],"mtcnote") == 0) &&
				dt > tdt){
				//search through M-aircraft list
				for(a = 0; a < numMacInfo; a++){
					if(atoi(row[whereAircraftid]) == macInfoList[a].aircraftID){
						//nbSkip++;
						skip = 1;
						//if (verbose) {
						//	fprintf(logFile, "Skipping OS record %d for aircraftID %d.\n", atoi(row[whereRec_id]), macInfoList[a].aircraftID);
						//}
						break;
					}
				}
				if (skip == 1)
					continue;
			}
		}
		//END - MAC - 01/07/09 ANG

		radPtrPrevious->aircraftid = radPtr->aircraftid;
     	radPtrPrevious->rec_outtime = radPtr->rec_outtime;

		oldTree = rawAircraftRoot;

		if(!(rawAircraftRoot = RBTreeInsert(rawAircraftRoot, radPtr, rawAircraftCompare))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readAcList(). tail: %s, rowtype: %s, outtime: %s\n",
				__FILE__,__LINE__, radPtr->registration, radPtr->rowtype,
		    		(radPtr->rec_outtime) ? dt_DateTimeToDateTimeString(radPtr->rec_outtime,tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
			logMsg(logFile,"\n Possible Reason: duplicate records in readACList().\n",__FILE__,__LINE__);
			sprintf(writetodbstring1, "%s Line %d, Duplicate demandID in readACList().",__FILE__,__LINE__);
		    if(errorNumber==0)
			{ if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		         {logMsg(logFile,"%s Line %d, Out of Memory in readACList().\n", __FILE__,__LINE__);
		          writeWarningData(myconn); exit(1);
	             }
			}				
		    else
			{   if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		         {logMsg(logFile,"%s Line %d, Out of Memory in readACList().\n", __FILE__,__LINE__);
		          writeWarningData(myconn); exit(1);
	             }
			}
			initializeWarningInfo(&errorinfoList[errorNumber]);
		    errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
            strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
		    sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
		    sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			errorinfoList[errorNumber].format_number=36;
            strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
			errorNumber++;
			writeWarningData(myconn); 
			rawAircraftRoot = oldTree;
			continue; 
			//exit(1);
		}

		tempAircraftID = (row[whereAircraftid]) ? atoi(row[whereAircraftid]) : 0;
		if(rows == 0)
			firstAircraftID = tempAircraftID;
		if(tempAircraftID != currentAircraftID) {
			++nbrUnqAc;
			currentAircraftID = tempAircraftID;
		}
		memcpy(&radBuf, radPtr, sizeof(radBuf));
		tmp = TreeSearch(rawAircraftRoot, &radBuf, rawAircraftCompare);

		tmp = Predecessor(tmp);
		if(tmp) {
			prevRadPtr = getTreeObject(tmp);
			if(prevRadPtr->aircraftid == radPtr->aircraftid) {
				DateTime this_takeoff_time;
				DateTime prev_landing_time;
				int days, hours, minutes, seconds, msecs;
				this_takeoff_time = (radPtr->actualout) ? radPtr->actualout : radPtr->rec_outtime;
				prev_landing_time = (prevRadPtr->actualin) ? prevRadPtr->actualin : prevRadPtr->rec_intime;
				if(this_takeoff_time && prev_landing_time) {
					dt_dateTimeDiff(this_takeoff_time, prev_landing_time, &days, &hours, &minutes, &seconds, &msecs);
					radPtr->minutes_since_previous_landing = (24 * 60 * days) + (60 * hours) + minutes;
				}
			}
		}
	}

	// allocate aircraft structs we will need
	tmpACList = (Aircraft *) calloc((size_t) nbrUnqAc + 1, (size_t) sizeof(Aircraft));
	if(! tmpACList) {
		logMsg(logFile,"%s Line %d, Out of Memory in readAcList().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	tmpACList->aircraftID = firstAircraftID;

	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning

	for(rows = 0, tPtr = tmpACList; rows < rowCount; rows++) {
		//row = mysql_fetch_row(res);

		//START - 02/28/08 ANG
		if (rows == 0) {
			row = mysql_fetch_row(res);
			nextRow = mysql_fetch_row(res);
		} else {
			row = nextRow;
			nextRow = mysql_fetch_row(res);
		}
		//END - 02/28/08 ANG*/

		if(! row)
			break;

		// experimental
        if(row[whereRec_outtime]) {
			dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[whereRec_outtime], NULL, &errNbr); //tdt: windowStart
			if(dt >= tdt)
				continue;
		}
		// end experimental

		//START - Skipping all records for MAC - MAC - 01/05/09 ANG
		if(optParam.ignoreMac == 1){
			skip = 0;
			tempACID = (row[whereAircraftid]) ? atoi(row[whereAircraftid]) : 0;
			if(tempACID > 0){
				//search through M-aircraft list
				for(a = 0; a < numMacInfo; a++){
					if(atoi(row[whereAircraftid]) == macInfoList[a].aircraftID){
						nbSkip++;
						skip = 1;
						//if (verbose) {
						//	fprintf(logFile, "Skipping OS record %d for aircraftID %d.\n", atoi(row[whereRec_id]), macInfoList[a].aircraftID);
						//}
						break;
					}
				}
				if (skip == 1)
					continue;
			}
		}
		//END - MAC - 01/05/09 ANG

		//START - Skipping OS - All or MAC - 11/04/08 ANG
		if(optParam.ignoreAllOS == 1){
			if (strcmp(row[whereRowtype],"ownersign") == 0){
				nbSkip++;
				continue;
			}
		}
		if(optParam.ignoreMacOS == 1 && optParam.withMac == 1 && optParam.ignoreAllOS == 0){
			skip = 0;
			if(strcmp(row[whereRowtype],"ownersign") == 0){
				//search through M-aircraft list
				for(a = 0; a < numMacInfo; a++){
					if(atoi(row[whereAircraftid]) == macInfoList[a].aircraftID){
						nbSkip++;
						skip = 1;
						if (verbose) {
							fprintf(logFile, "Skipping OS record %d for aircraftID %d.\n", atoi(row[whereRec_id]), macInfoList[a].aircraftID);
						}
						break;
					}
				}
				if (skip == 1)
					continue;
			}
		}
		//END - MAC - 11/04/08 ANG

		//START - Add condition to ignore Mac's OS and MX if started after windowStart - MAC - 01/07/09 ANG
		if(optParam.ignoreMacOSMX == 1 && optParam.ignoreMac == 0 && optParam.withMac == 1){
			skip = 0;
	        if(row[whereRec_outtime]) {
				dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[whereRec_outtime], NULL, &errNbr); //tdt: windowStart
			}
			if((strcmp(row[whereRowtype],"ownersign") == 0 || strcmp(row[whereRowtype],"mtcnote") == 0) &&
				dt > tdt){
				//search through M-aircraft list
				for(a = 0; a < numMacInfo; a++){
					if(atoi(row[whereAircraftid]) == macInfoList[a].aircraftID){
						nbSkip++;
						skip = 1;
						break;
					}
				}
				if (skip == 1)
					continue;
			}
		}
		//END - MAC - 01/07/09 ANG

		currentAircraftID = (row[whereAircraftid]) ? atoi(row[whereAircraftid]) : 0;
		if(currentAircraftID != tPtr->aircraftID) {
			if(tPtr->availAirportID == 0) {
				// check the lost plane list
				int ix;
				lpPtr = lostPlanesList;

				for(ix = 0; ix < lostPlanesCount; ++ix, ++lpPtr) {
					if(lpPtr->aircraftid != tPtr->aircraftID)
						continue;
					// experimental
					if(lpPtr->leg_outtime) {
						dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", lpPtr->leg_outtime, NULL, &errNbr);
						if(dt > tdt)
							continue;
					}
					// end experimental
					tPtr->aircraftTypeID = lpPtr->leg_ac_type; 
					//(lpPtr->leg_ac_type) ? lpPtr->leg_ac_type : 5;
					//Bug: Temp fix. Roger
					//Can not be zero. 
					tPtr->availAirportID = lpPtr->leg_inaptid;
					tPtr->availFboID = lpPtr->leg_infboid; //populate the fbo
					if(lpPtr->leg_inicao && strlen(lpPtr->leg_inicao))
						strcpy(tPtr->availAptICAO, lpPtr->leg_inicao);
	
					tPtr->availDT = 0;
					intime[0] = '\0';
					addTaxiInTm = 0;
					if(strcmp(lpPtr->legtype,"aircraft") != 0) {
						/* calculate time_t availDT */
						if(strncmp(lpPtr->legtype,"mgdleg", 6) == 0 || strcmp(lpPtr->legtype,"mtcnote") == 0 || strcmp(lpPtr->legtype,"aptasgn") == 0)
							strcpy(intime, lpPtr->leg_intime);
						else if(strncmp(lpPtr->legtype,"logmgdleg", 9) == 0) {
							if(lpPtr->actualin)
								strcpy(intime, lpPtr->actualin);
							else if(lpPtr->actualon) {
								strcpy(intime, lpPtr->actualon);
								addTaxiInTm = 1;
							}
							else if(lpPtr->leg_intime)
								strcpy(intime, lpPtr->leg_intime);
							else {
								// eventually we will do our own flight calc here instead of err (if we have scheduledout and orig apt and dest apt)
								logMsg(logFile,"%s Line %d: Bad logpageleg record.\n", __FILE__, __LINE__);
								sprintf(writetodbstring1, "%s Line %d, Bad logpageleg record.", __FILE__, __LINE__);
								if(errorNumber==0)
								{ if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                           {logMsg(logFile,"%s Line %d, Out of Memory in readACList().\n", __FILE__,__LINE__);
		                            writeWarningData(myconn); exit(1);
	                               }
								}
								else
								{ if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                           {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		                            writeWarningData(myconn); exit(1);
	                               }
								}
								initializeWarningInfo(&errorinfoList[errorNumber]);
		                         errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                                 strcpy(errorinfoList[errorNumber].group_name, "group_debugging");
				                 sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				                 sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			                     errorinfoList[errorNumber].format_number=20;
                                 strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
								 errorNumber++;
							}
						}
						if(intime[0]) {
							dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", intime, NULL, &errNbr);
							if(dt == BadDateTime) {
								logMsg(logFile,"%s Line %d:Bad Date time: %s. errNbr = %d\n", __FILE__,__LINE__,intime, errNbr);
							}
							else {
								if(addTaxiInTm) {
									dt = dt_addToDateTime(Minutes, optParam.taxiInTm, dt);
								}
								tPtr->availDT = DateTimeToTime_t(dt);
							}
						}
					}
			
					if(strcmp(lpPtr->legtype,"mtcnote") == 0)
						tPtr->maintFlag = 1;
					else if(strcmp(lpPtr->legtype,"aptasgn") == 0)
						tPtr->maintFlag = 2;
					else
						tPtr->maintFlag = 0;
					tPtr->sequencePosn = lpPtr->leg_pos;
					if(lpPtr->leg_outtime && strcmp(lpPtr->leg_outtime,"0000/00/00 00:00") != 0) {
						tPtr->rec_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", lpPtr->leg_outtime, NULL, &errNbr);
						if(tPtr->rec_outtime == BadDateTime) {
							logMsg(logFile,"%s Line %d: Bad Date time: %s. errNbr = %d\n", __FILE__,__LINE__,lpPtr->leg_outtime, errNbr);
						}
					}
				}
			}

			++tPtr;
		}

		tPtr->aircraftID = currentAircraftID;
		tPtr->aircraftTypeID = (row[whereAc_type]) ? atoi(row[whereAc_type]) : 0; //Bug: Roger 12/16
		tPtr->availAirportID = (row[whereInaptid]) ? atoi(row[whereInaptid]) : 0;
		tPtr->availFboID = (row[whereInfboid]) ? atoi(row[whereInfboid]) : 0;
		if(row[whereInicao])
			strcpy(tPtr->availAptICAO, row[whereInicao]);

		tPtr->availDT = 0;
		intime[0] = '\0';
		nextOutTime[0] = '\0'; // 02/28/08 ANG
		addTaxiInTm = 0;
		if(strcmp(row[whereRowtype],"aircraft") != 0) {
			/* calculate time_t availDT */
			if(strcmp(row[whereRowtype],"mgdleg") == 0 || strcmp(row[whereRowtype],"mtcnote") == 0 || strcmp(row[whereRowtype],"ownersign") == 0) //Roger: Jintao's fix for ac-availDT
				strcpy(intime, row[whereRec_intime]);
			else if(strcmp(row[whereRowtype],"logmgdleg") == 0) {
				if(row[whereActualin])
					strcpy(intime, row[whereActualin]);
				else if(row[whereActualon]) {
					strcpy(intime, row[whereActualon]);
					addTaxiInTm = 1;
				}
				else if(row[whereRec_intime])
					strcpy(intime, row[whereRec_intime]);
				else {
					// eventually we will do our own flight calc here instead of err (if we have scheduledout and orig apt and dest apt)
					logMsg(logFile,"%s Line %d: Bad logpageleg record.\n",__FILE__, __LINE__);
					sprintf(writetodbstring1, "%s Line %d, Bad logpageleg record.", __FILE__, __LINE__);
				    if(errorNumber==0)
					{if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                {logMsg(logFile,"%s Line %d, Out of Memory in readACList().\n", __FILE__,__LINE__);
		                 writeWarningData(myconn); exit(1);
	                    }
					}		
					else
					{if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		                 writeWarningData(myconn); exit(1);
	                    }
					}
					initializeWarningInfo(&errorinfoList[errorNumber]);
		            errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                    strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				    sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				    sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			        errorinfoList[errorNumber].format_number=20;
                    strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				    errorNumber++;
				}
			}
			if(intime[0]) {
				dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", intime, NULL, &errNbr);
				if(dt == BadDateTime) {
					logMsg(logFile,"%s Line %d: Bad Date time: %s. errNbr = %d\n", __FILE__,__LINE__,intime, errNbr);
				}
				else {
					if(addTaxiInTm) {
						dt = dt_addToDateTime(Minutes, optParam.taxiInTm, dt);
					}
					tPtr->availDT = DateTimeToTime_t(dt);
					//START - Populate next leg's start time - 02/28/08 ANG
					//RLZ: This is taken care by existing tour info.
					/*
					if( nextRow && strcmp(nextRow[whereRowtype],"aircraft") != 0 && 
						currentAircraftID == ((nextRow[whereAircraftid]) ? atoi(nextRow[whereAircraftid]) : 0)){
						strcpy(nextOutTime, nextRow[whereRec_outtime]);
						if(nextOutTime[0]){
							ndt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", nextOutTime, NULL, &errNbr);
							if(ndt == BadDateTime) {
								logMsg(logFile,"%s Line %d: Bad Date time: %s. errNbr = %d\n", __FILE__,__LINE__,intime, errNbr);
							}
							else{
								tPtr->nextLegStartTm = DateTimeToTime_t(ndt);
							}
						}
					}
					*/
					//END - 02/28/08 ANG*/
				}
			}
		}

		if(strcmp(row[whereRowtype],"mtcnote") == 0)
			tPtr->maintFlag = 1;
		else if(strcmp(row[whereRowtype],"ownersign") == 0)
			tPtr->maintFlag = 2;
		else
			tPtr->maintFlag = 0;
		tPtr->sequencePosn = (row[whereSequenceposition]) ? atoi(row[whereSequenceposition]) : 0;
		if(row[whereRec_outtime]) {
			tPtr->rec_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[whereRec_outtime], NULL, &errNbr);
			if(tPtr->rec_outtime == BadDateTime) {
				logMsg(logFile,"%s Line %d: Bad Date time: %s. errNbr = %d\n", __FILE__,__LINE__,row[whereRec_outtime], errNbr);
			}
		}
	}

	// free mysql results
	mysql_free_result(res);

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//
	// Remove planes from list for which you only had an 'aircraft' record (see SQL statement).
	// Make a note in the log of the planes that were removed (essentially, planes that couldn't be found).
	//
	for(tPtr = tmpACList, idx = 0;tPtr->aircraftID;tPtr++) {
		if(tPtr->availAirportID)
			++idx;
		else
		{logMsg(logFile,"readACList(): Couldn't find managed aircraft with aircraftID %d\n", tPtr->aircraftID);
		  sprintf(writetodbstring1, "readACList(): Couldn't find managed aircraft with aircraftID %d", tPtr->aircraftID);
		 if(errorNumber==0)
		 { if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		      {logMsg(logFile,"%s Line %d, Out of Memory in readACList().\n", __FILE__,__LINE__);
		       writeWarningData(myconn); exit(1);
	          }
		 }					
		 else
		 { if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		        {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		         writeWarningData(myconn); exit(1);
	            }	
		 }
	  initializeWarningInfo(&errorinfoList[errorNumber]);
		errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
		strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
		errorinfoList[errorNumber].aircraftid=tPtr->aircraftID;
		errorinfoList[errorNumber].format_number=22;
		strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
		errorNumber++;
		}
	}
	// allocate aircraft structs we will need
	aircraftCount = idx;
	acList = (Aircraft *) calloc((size_t) idx + 1, (size_t) sizeof(Aircraft));
	if(! acList) {
		logMsg(logFile,"%s Line %d, Out of Memory in readAcList().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	for(tPtr = tmpACList, tPtr2 = acList;tPtr->aircraftID;tPtr++) {
				
		//MAC - 08/19/08 ANG
		//Identify which aircraft is Mac by checking macInfoList
		if(optParam.withMac == 1){
			tPtr->hasOwnerTrip = 0; //initialize
			tPtr->isMac = 0;
			for(a = 0; a < numMacInfo; a++){
				if(tPtr->aircraftID == macInfoList[a].aircraftID){
					tPtr->isMac = 1;
					break;
				}
			}
		}

		if(tPtr->availAirportID) {
			memcpy(tPtr2, tPtr, sizeof(Aircraft));
			/////////////////////////////////////////////////////////////////////////////
			// populate intlCert member of Aircraft struct
			/////////////////////////////////////////////////////////////////////////////
			tmp = TreeSearch(intnlCertRoot, &(tPtr2->aircraftID), integerCmp);
			if(tmp)
				tPtr2->intlCert = 1;
			else
				tPtr2->intlCert = 0; 
			/////////////////////////////////////////////////////////////////////////////
			tPtr2 ->legCrewPairFlag = -1; //Default value, RLZ 10/30/2007
			tPtr2++;
		}
	}
	free(tmpACList);
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	//printAcList(); // 02/28/08 ANG
	initAcSchedLegIndList(); //Initialize value for schedLegIndList for aircraft - 03/05/08 ANG

	/*if(verbose) {
		int x;
		int errNbr;
		logMsg(logFile,"acList:\n");
		//fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+----------+\n");
		//fprintf(logFile,"| aircraftID | aircraftTypeID | sequencePosn | availAirportID | ICAO  | availFboID | availDt          | maintFlag | intlcert |\n");
		//fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+----------+\n");
		fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+------------------+-----------+----------+\n"); // 02/28/08 ANG
		fprintf(logFile,"| aircraftID | aircraftTypeID | sequencePosn | availAirportID | ICAO  | availFboID | availDt          | nextLegStartTm   | maintFlag | intlcert |\n"); // 02/28/08 ANG
		fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+------------------+-----------+----------+\n"); // 02/28/08 ANG
		for(x = 0, tPtr = acList; x < idx; ++x, ++tPtr) {
			fprintf(logFile,"| %10d | %14d | %12d | %14d | %5s | %10d | %16s | %16s | %9d | %8d |\n",
				tPtr->aircraftID,
				tPtr->aircraftTypeID,
				tPtr->sequencePosn,
				tPtr->availAirportID,
				tPtr->availAptICAO,
				tPtr->availFboID,
				(tPtr->availDT) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				(tPtr->nextLegStartTm) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->nextLegStartTm))), NULL, &errNbr),tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				tPtr->maintFlag,
				tPtr->intlCert);
		}
		//fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+----------+\n\n");
		fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+------------------+-----------+----------+\n"); // 02/28/08 ANG
		fflush(logFile);
	}*/

	//numAircraft = nbrUnqAc;
	numAircraft = idx;

	// add airports to airport list
	for(nbrUnqAc = 0, tPtr = acList; nbrUnqAc < numAircraft; nbrUnqAc++, ++tPtr) {
		if(! tPtr->availAirportID)
			continue;
		aptBuf.airportID = tPtr->availAirportID;
		if(TreeSearch(airportRoot, &aptBuf, airportCompare))
			continue;
		aptPtr = calloc((size_t) 1, sizeof(Airport));
		if(! aptPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readAcList().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		aptPtr->airportID = tPtr->availAirportID;
		strcpy(aptPtr->ICAO,tPtr->availAptICAO);
		if(!(airportRoot = RBTreeInsert(airportRoot, aptPtr, airportCompare))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readAcList().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		numAirports++;
	}
	
	
	return(0);
}
/********************************************************************************
*	Function   readMaintList              Date last modified:   /  /06 SWO
*	Purpose:  Read in Aircraft Maintenance List from mySQL db.
********************************************************************************/
typedef enum {
	mtcAircraftmtcnoteid = 0, mtcAircraftid, mtcAirportid, mtcStarttime, mtcEndtime, mtcApptType, mtcFboid, mtc_end_of_list = 255
} mtcSqlColumns;
static int readMaintList(MY_CONNECTION *myconn)
{

	extern char *mtcnoteSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	MaintenanceRecord *tPtr;

	DateTime dt;
	int errNbr = 0;
	int a, skip;
	int nbMxSkip = 0;

	if(!myDoQuery(myconn, mtcnoteSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readMaintList(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readMaintList(): 0 rows returned.\n");
		return(0);
	}

	//maintList = (MaintenanceRecord *) calloc((size_t) rowCount + 1, (size_t) sizeof(MaintenanceRecord));
	maintList = (MaintenanceRecord *) calloc((size_t) rowCount + optParam.maxFakeMaintRec, (size_t) sizeof(MaintenanceRecord));// 11/06/07 ANG
	if(! maintList) {
		logMsg(logFile,"Out of Memory in readMaintList().\n");
		writeWarningData(myconn); exit(1);
	}
	//for(rows = 0, tPtr = maintList; rows < rowCount; rows++, tPtr++) {
	for(rows = 0, tPtr = maintList; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;

		//START - Skipping records for aircraft not listed in acList - 11/26/08 ANG
		skip = 1;
		for (a = 0; a < numAircraft; a++){
			if(atoi(row[mtcAircraftid]) == acList[a].aircraftID){
				skip = 0;
				break;
			}
		}
		if(skip == 1){
			nbMxSkip++;
			continue;
		}
		//END - 11/26/08 ANG

		//START - 10/28/08 ANG
		if(optParam.ignoreAllOS == 1){
			if(atoi(row[mtcApptType]) == 1){
				nbMxSkip++;
				continue;
			}
		}
		//END - 10/28/08 ANG

		//START - MAC - 10/23/08 ANG
		if(optParam.ignoreMacOS == 1 && optParam.withMac == 1 && optParam.ignoreAllOS == 0){
			skip = 0;
			if(atoi(row[mtcApptType]) == 1){
				//search through M-aircraft list
				for(a = 0; a < numMacInfo; a++){
					if(atoi(row[mtcAircraftid]) == macInfoList[a].aircraftID){
						nbMxSkip++;
						skip = 1;
						fprintf(logFile, "Skipping OS record %d for aircraftID %d.\n", atoi(row[mtcAircraftmtcnoteid]), macInfoList[a].aircraftID);
						break;
					}
				}
				if (skip == 1)
					continue;
			}
		}
		//END - MAC - 10/23/08 ANG

		//START - Add condition to ignore Mac's OS and MX if started after windowStart - MAC - 01/07/09 ANG
		if(optParam.ignoreMacOSMX == 1 && optParam.ignoreMac == 0 && optParam.withMac == 1){
			skip = 0;
	        //if(row[whereRec_outtime]) {
			//	dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[whereRec_outtime], NULL, &errNbr); //tdt: windowStart
			//}
			dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[mtcStarttime], NULL, &errNbr);
			if(dt == BadDateTime) {
				logMsg(logFile,"Bad Date time: %s. errNbr = %d\n", row[mtcStarttime], errNbr);
			}
			if(dt > dt_time_tToDateTime(optParam.windowStart)){
				//search through M-aircraft list
				for(a = 0; a < numMacInfo; a++){
					if(atoi(row[mtcAircraftid]) == macInfoList[a].aircraftID){
						nbMxSkip++;
						skip = 1;
						fprintf(logFile, "Skipping OS/MX record %d for aircraftID %d.\n", atoi(row[mtcAircraftmtcnoteid]), macInfoList[a].aircraftID);
						break;
					}
				}
				if (skip == 1)
					continue;
			}
		}
		//END - MAC - 01/07/09 ANG

		tPtr->maintenanceRecordID = (row[mtcAircraftmtcnoteid]) ? atoi(row[mtcAircraftmtcnoteid]) : 0;
		tPtr->aircraftID = (row[mtcAircraftid]) ? atoi(row[mtcAircraftid]) : 0;
		tPtr->airportID = (row[mtcAirportid]) ? atoi(row[mtcAirportid]) : 0;

		/* calculate time_t startTm */
		tPtr->startTm = 0;
		dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[mtcStarttime], NULL, &errNbr);
		if(dt == BadDateTime) {
			logMsg(logFile,"Bad Date time: %s. errNbr = %d\n", row[mtcStarttime], errNbr);
		}
		else {
			tPtr->startTm = DateTimeToTime_t(dt);
		}

		/* calculate time_t endTm */
		tPtr->endTm = 0;
		dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[mtcEndtime], NULL, &errNbr);
		if(dt == BadDateTime) {
			logMsg(logFile,"Bad Date time: %s. errNbr = %d\n", row[mtcEndtime], errNbr);
		}
		else {
			tPtr->endTm = DateTimeToTime_t(dt);
		}
		tPtr->apptType = atoi(row[mtcApptType]);
		tPtr->fboID = (row[mtcFboid]) ? atoi(row[mtcFboid]):0;

		tPtr++;

	}
	if(verbose) {
		int x;
		int errNbr1;
		int errNbr2;
		char tbuf1[32];
		char tbuf2[32];

		logMsg(logFile,"maintList:\n");
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+\n");
		fprintf(logFile,"| maintRecordID | aircraftID | airportID |          startTm |            endTm | apptType |\n");
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+\n");
		for(x = 0, tPtr = maintList; x < rowCount-nbMxSkip; ++x, ++tPtr) {

			fprintf(logFile,"| %13d | %10d | %9d | %16s | %16s | %8d |\n",
				tPtr->maintenanceRecordID,
				tPtr->aircraftID,
				tPtr->airportID,
				(tPtr->startTm) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->startTm))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				(tPtr->endTm) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->endTm))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				tPtr->apptType);
		}
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+\n\n\n\n");
		fflush(logFile);
	}

	numMaintenanceRecord = (int) (rowCount - nbMxSkip);

	// free mysql results
	mysql_free_result(res);
	return 0;

}

/********************************************************************************
*	Function   readRecoveryDemandList              Date last modified: RLZ
*	Purpose:  Read in RecoveryDemand List from mySQL db.
********************************************************************************/

static int readRecoveryDemandList(int* recoveryDemandNum)
{
	int x;	
	char tbuf[1024],*wptrs[128];
	char *paramVal;
	extern char *getCmdLineVar(char *varName);

	if(verbose) logMsg(logFile,"** Recovery Demand read in by readRecoveryDemandList:\n");

	if(!(paramVal = getCmdLineVar("recoveryDemandIDs"))) {
		logMsg(logFile,"No recovery demands.\n");
		recoveryDemandNum = 0;
		return 0;
	}
	if(verbose) logMsg(logFile,"recoveryDemandList = %s\n", paramVal);
	strcpy(tbuf, paramVal);
	*recoveryDemandNum = split(tbuf," \t", wptrs);
	recoveryDemandList = (int *) calloc(*recoveryDemandNum, sizeof(int));
	if(! recoveryDemandList) {
		logMsg(logFile,"Out of Memory in readRecoveryDemandList().\n");
		writeWarningData(myconn); 
		exit(1);
	}

	for(x = 0; x < *recoveryDemandNum; x++) {
		recoveryDemandList[x] = atoi(wptrs[x]);
	}
	return 0;
}



/********************************************************************************
*	Function   readDemandList              Date last modified:
*	Purpose:  Read in Demand List from mySQL db.
********************************************************************************/
typedef enum {
	dmdDemandid = 0, dmdOwnerid, dmdContractid, dmdOutaptid, dmdOutfboid, dmdInaptid, dmdInfboid, dmdOuttime,
	dmdIntime, dmdFlexschedule, dmdAircrafttypeid, dmdSequenceposition, dmdAc_type, dmdNbrpsgrs,
	dmdOutcountryid, dmdIncountryid, dmdOutRunway, dmdInRunway, dmd_end_of_list = 255
} dmdSqlColumns;
static int readDemandList(MY_CONNECTION *myconn)
{
	extern char *demandSQL;

	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	int idx;
	Demand *tPtr;
	DateTime dt;
	int errNbr;
	char *p;
    char writetodbstring1[200];
	//int allocmore;
	int max_FAKE_DMD_NUM;

	//int *recoveryDemandList = NULL;
	int recoveryDemandNum, ri;
	int a, b; //MAC - 09/02/08 ANG	
	int skip, tempCtrID; //MAC - 01/06/09 ANG

	maxDemandID = 0;
	max_FAKE_DMD_NUM = 0;
	if (optParam.runWithContingency)
		max_FAKE_DMD_NUM = 80;


	readRecoveryDemandList(&recoveryDemandNum);

	if(!myDoQuery(myconn, demandSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readDemandList(): 0 rows returned.\n");
		return(0);
	}

	numDemand = (int) rowCount;

	//demandList = (Demand *) calloc((size_t) rowCount + 1, (size_t) sizeof(Demand));
	//numDemandAllocd = (2*numDemand) + (2*numMaintenanceRecord);
	//numDemandAllocd = (2*numDemand) + (2*numMaintenanceRecord) + (2*optParam.maxFakeMaintRec); // 11/01/07 ANG
	numDemandAllocd = (2*numDemand) +(2*max_FAKE_DMD_NUM)+(2*numMaintenanceRecord) + (2*optParam.maxFakeMaintRec); // 03/28/08 Jintao
	demandList = (Demand *)calloc((size_t) numDemandAllocd,sizeof(Demand));
	if(!demandList) {
		logMsg(logFile,"Out of Memory in readDemandList().\n");
		writeWarningData(myconn); exit(1);
	}

	//initialize arrays in Demand objects
	for(idx = 0, tPtr = demandList; idx < numDemandAllocd; ++idx, tPtr++) {
		if((tPtr->blockTm = calloc((size_t) numAcTypes + 1, sizeof(int))) == NULL) {
			logMsg(logFile,"%s Line %d, Out of Memory in readDemandList().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
		if((tPtr->elapsedTm = calloc((size_t) numAcTypes + 1, sizeof(int))) == NULL) {
			logMsg(logFile,"%s Line %d, Out of Memory in readDemandList().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}

		if((tPtr->early = calloc((size_t) numAcTypes + 1, sizeof(int))) == NULL) { 
			logMsg(logFile,"%s Line %d, Out of Memory in readDemandList().\n", __FILE__, __LINE__); 
			writeWarningData(myconn); exit(1); 
		} 
        
		if((tPtr->late = calloc((size_t) numAcTypes + 1, sizeof(int))) == NULL) { 
			logMsg(logFile,"%s Line %d, Out of Memory in readDemandList().\n", __FILE__, __LINE__); 
			writeWarningData(myconn); exit(1); 
		} 

		if((tPtr->cost = calloc((size_t) numAcTypes + 1, sizeof(double))) == NULL) {
			logMsg(logFile,"%s Line %d, Out of Memory in readDemandList().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
		//START - MAC - 08/19/08 ANG
		if((tPtr->macOprCost = calloc((size_t) numAcTypes + 1, sizeof(double))) == NULL) {
			logMsg(logFile,"%s Line %d, Out of Memory in readDemandList().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
		//if((tPtr->macOwnerCost = calloc((size_t) numAcTypes + 1, sizeof(double))) == NULL) {
		//	logMsg(logFile,"%s Line %d, Out of Memory in readDemandList().\n", __FILE__, __LINE__);
		//	writeWarningData(myconn); exit(1);
		//}
		//END - MAC		
	}

	//for(rows = 0, tPtr = demandList; rows < rowCount; rows++, tPtr++) {
	for(rows = 0, tPtr = demandList; rows < rowCount; rows++) { // 09/09/08 ANG
		row = mysql_fetch_row(res);
		if(! row)
			break;

		//Exclude contingency demands - 09/09/08 ANG
		//if (optParam.inclContingencyDmd == 0 && row[dmdOwnerid] && (atoi(row[dmdOwnerid]) == 87359 || atoi(row[dmdOwnerid]) == 56801 || atoi(row[dmdOwnerid]) == 24892)){
		//Contingency demand should be built exclusively under 87359
		if (optParam.inclContingencyDmd == 0 && row[dmdOwnerid] && (atoi(row[dmdOwnerid]) == 87359 )){
			numDemand--;
			continue;
		}

		//Exclude mac demands - MAC - 01/06/09 ANG
		if(optParam.ignoreMacDmd == 1){
			//Check contractID for the demand
			skip = 0;
			tempCtrID = (row[dmdContractid]) ? atoi(row[dmdContractid]) : 0;
			if(tempCtrID > 0){
				//search through M-aircraft list
				for(a = 0; a < numMacInfo; a++){
					if(tempCtrID == macInfoList[a].contractID){
						skip = 1;
						break;
					}
				}
				if (skip == 1){
					numDemand--;
					continue;
				}
			}
		}

		tPtr->demandID = (row[dmdDemandid]) ? atoi(row[dmdDemandid]) : 0;
		if(tPtr->demandID > maxDemandID)
			maxDemandID = tPtr->demandID;
		tPtr->ownerID = (row[dmdOwnerid]) ? atoi(row[dmdOwnerid]) : 0;
		tPtr->contractID = (row[dmdContractid]) ? atoi(row[dmdContractid]) : -1;
		tPtr->outAirportID = (row[dmdOutaptid]) ? atoi(row[dmdOutaptid]) : 0;
		tPtr->outFboID = (row[dmdOutfboid]) ? atoi(row[dmdOutfboid]) : 0;
		tPtr->inAirportID = (row[dmdInaptid]) ? atoi(row[dmdInaptid]) : 0;
		tPtr->inFboID = (row[dmdInfboid]) ? atoi(row[dmdInfboid]) : 0;

		if(tPtr->ownerID == 24299) countTestFlights++; //05/23/08 ANG
		
		//MAC - 08/19/08 ANG
		//Identify Mac demands by checking contract IDs in macInfoList
		if(optParam.withMac == 1){
			tPtr->isMacDemand = 0;
			for(a = 0; a < numMacInfo; a++){
				if(tPtr->contractID == macInfoList[a].contractID){
					tPtr->isMacDemand = 1;
					tPtr->macID = macInfoList[a].aircraftID;
					fprintf(logFile, "Mac demand: %d\n", tPtr->demandID);

					//populate aircraft's hasOwnerTrip field
					for(b = 0; b < numAircraft; b++){
						if(acList[b].aircraftID == macInfoList[a].aircraftID){
							acList[b].hasOwnerTrip = 1;
							break;
						}
					}

					break;
				}
			}		
		}

		/* calculate time_t reqOut */
		tPtr->reqOut = 0;
		dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[dmdOuttime], NULL, &errNbr);
		if(dt == BadDateTime) {
			logMsg(logFile,"Bad Date time: %s. errNbr = %d\n", row[dmdOuttime], errNbr);
		}
		else {
			tPtr->reqOut = DateTimeToTime_t(dt);
		}

		/* calculate time_t reqIn */
		tPtr->reqIn = 0;
		dt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[dmdIntime], NULL, &errNbr);
		if(dt == BadDateTime) {
			logMsg(logFile,"Bad Date time: %s. errNbr = %d\n", row[dmdIntime], errNbr);
		}
		else {
			tPtr->reqIn = DateTimeToTime_t(dt);
		}

		tPtr->contractFlag = (row[dmdFlexschedule]) ? atoi(row[dmdFlexschedule]) : 0;
		tPtr->aircraftTypeID  = (row[dmdAircrafttypeid]) ? atoi(row[dmdAircrafttypeid]) : 0; //Bug, Roger
		tPtr->sequencePosn  = (row[dmdSequenceposition]) ? atoi(row[dmdSequenceposition]) : 0;
		tPtr->numPax  = (row[dmdNbrpsgrs]) ? atoi(row[dmdNbrpsgrs]) : 0;

		//Assume all trips in demand is within fleet capacity, therefore update number of pax if exceed max capacity - 02/03/09 ANG 
		//tPtr->numTruePax = (row[dmdNbrpsgrs]) ? atoi(row[dmdNbrpsgrs]) : 0;
		//for(ri = 0; ri < numAcTypes; ri++){
		//	if( acTypeList[ri].aircraftTypeID == tPtr->aircraftTypeID){
		//		if (acTypeList[ri].capacity < tPtr->numPax)
		//			tPtr->numPax = acTypeList[ri].capacity;
		//		break;
		//	}
		//}

		p = aptIdToIcao(tPtr->outAirportID);
		if(p)
			strcpy(tPtr->outAptICAO, p);
		else
			logMsg(logFile,"%s Line %d: airportID %d not found.\n", __FILE__,__LINE__, tPtr->outAirportID);
		p = aptIdToIcao(tPtr->inAirportID);
		if(p)
			strcpy(tPtr->inAptICAO, p);
		else
			logMsg(logFile,"%s Line %d: airportID %d not found.\n", __FILE__,__LINE__, tPtr->inAirportID);
		tPtr->outCountryID = (row[dmdOutcountryid]) ? atoi(row[dmdOutcountryid]) : 0;
		tPtr->inCountryID = (row[dmdIncountryid]) ? atoi(row[dmdIncountryid]) : 0;

		if ( (atoi(row[dmdOutRunway]) > 0 && atoi(row[dmdOutRunway]) < 5000) || (atoi(row[dmdInRunway]) > 0 && atoi(row[dmdInRunway]) < 5000))
			tPtr->noCharterFlag = 1;
		if(!(dmdXDemandIdRoot = RBTreeInsert(dmdXDemandIdRoot, tPtr, demandIdCompare))) {
			logMsg(logFile,"%s Line %d, Duplicate demandID in readDemandList().\n",__FILE__,__LINE__);
			sprintf(writetodbstring1, "%s Line %d, Duplicate demandID in readDemandList().",__FILE__,__LINE__);
		    if(errorNumber==0)
			{ if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		         {logMsg(logFile,"%s Line %d, Out of Memory in readDemandList().\n", __FILE__,__LINE__);
		          writeWarningData(myconn); exit(1);
	             }
			}				
		    else
			{   if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		         {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		          writeWarningData(myconn); exit(1);
	             }
			}
			initializeWarningInfo(&errorinfoList[errorNumber]);
			errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
			strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
			sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
			sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			errorinfoList[errorNumber].format_number=23;
			strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
			errorNumber++;
			writeWarningData(myconn); 
			exit(1);
		}
		
		//TEMPZHAN
		//if (tPtr->demandID == 154349 || tPtr->demandID == 154324)
		//	tPtr->recoveryFlag = 1;

		//if (recoveryDemandNum){
			for (ri = 0; ri < recoveryDemandNum; ri++)
				if (tPtr->demandID == recoveryDemandList[ri])
					tPtr->recoveryFlag = 1;
		//}
		
		getPeakDayAdjustment(tPtr); //And recovery adjustment
		tPtr++; //09/09/08 ANG
	}

	//START - to capture demands related to test flights - 05/23/08 ANG
	if(countTestFlights > 0){
		int x, y;
		testFlightList = (Demand *)calloc((size_t) countTestFlights,sizeof(Demand));
		if(!testFlightList) {
			logMsg(logFile,"Out of Memory in creating testFlightList.\n");
			writeWarningData(myconn); exit(1);
		}
		y = 0;
		for(x = 0; x < rowCount; ++x) {
			if(demandList[x].ownerID == 24299){
				testFlightList[y] = demandList[x];
				y++;
			}
		}
	}

	if(verbose) {
		int x;
		int errNbr1;
		int errNbr2;
		char tbuf1[32];
		char tbuf2[32];

		logMsg(logFile,"\n testFlightList:\n");
		fprintf(logFile,"\
+--------+--------+--------+-----+--------+--------+--------+--------+--------+--------+------------------+------------------+----------+----------+----------+-------+-------+-------+-------+-------|\n");
		fprintf(logFile,"\
| demand |  owner | contra | num | outAir | outApt | outFbo | inAir  | inApt  |  inFbo |                  |                  | contract | aircraft | sequence |   Out |    In | Early |  Late |  No   |\n");
		fprintf(logFile,"\
|     ID |     ID |     ID | Pax | portID | ICAO   |     ID | portID | ICAO   |     ID |           reqOut |            reqIn | Flag     | TypeID   |     Posn | CtyId | CtyId |   Adj |   Adj | Chrtr |\n");
		fprintf(logFile,"\
+--------+--------+--------+-----+--------+--------+--------+--------+--------+--------+------------------+------------------+----------+----------+----------+-------+-------+-------+-------+-------|\n");
		for(x = 0, tPtr = testFlightList; x < countTestFlights; ++x, ++tPtr) {
			fprintf(logFile,"| %6d | %6d | %6d | %3d | %6d | %-6s | %6d | %6d | %-6s | %6d | %s | %s | %8d | %8d | %8d | %5d | %5d | %5d | %5d | %5d |\n",
				tPtr->demandID,
				tPtr->ownerID,
				tPtr->contractID,
				tPtr->numPax,
				tPtr->outAirportID,
				tPtr->outAptICAO,
				tPtr->outFboID,
				tPtr->inAirportID,
				tPtr->inAptICAO,
				tPtr->inFboID,
				(tPtr->reqOut) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->reqOut))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				(tPtr->reqIn) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->reqIn))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				tPtr->contractFlag,
				tPtr->aircraftTypeID,
				tPtr->sequencePosn,
				tPtr->outCountryID,
				tPtr->inCountryID,
				tPtr->earlyAdj,
				tPtr->lateAdj,
				tPtr->noCharterFlag);
		}
		fprintf(logFile,"\
+--------+--------+--------+-----+--------+--------+--------+--------+--------+--------+------------------+------------------+----------+----------+----------+-------+-------+-------+-------+-------|\n");
		fflush(logFile);
	}	
	//END - to capture demands related to test flights - 05/23/08 ANG

	if(optParam.runWithContingency == 1)
		readContingencyFkDemand(myconn);  //Jintao 3/11
	if(verbose) {
		int x;
		int errNbr1;
		int errNbr2;
		char tbuf1[32];
		char tbuf2[32];

		logMsg(logFile,"demandList:\n");
		fprintf(logFile,"\
+--------+--------+--------+-----+--------+--------+--------+--------+--------+--------+------------------+------------------+----------+----------+----------+-------+-------+-------+-------+-------|\n");
		fprintf(logFile,"\
| demand |  owner | contra | num | outAir | outApt | outFbo | inAir  | inApt  |  inFbo |                  |                  | contract | aircraft | sequence |   Out |    In | Early |  Late |  No   |\n");
		fprintf(logFile,"\
|     ID |     ID |     ID | Pax | portID | ICAO   |     ID | portID | ICAO   |     ID |           reqOut |            reqIn | Flag     | TypeID   |     Posn | CtyId | CtyId |   Adj |   Adj | Chrtr |\n");
		fprintf(logFile,"\
+--------+--------+--------+-----+--------+--------+--------+--------+--------+--------+------------------+------------------+----------+----------+----------+-------+-------+-------+-------+-------|\n");
		for(x = 0, tPtr = demandList; x < rowCount; ++x, ++tPtr) {
			fprintf(logFile,"| %6d | %6d | %6d | %3d | %6d | %-6s | %6d | %6d | %-6s | %6d | %s | %s | %8d | %8d | %8d | %5d | %5d | %5d | %5d | %5d |\n",
				tPtr->demandID,
				tPtr->ownerID,
				tPtr->contractID,
				tPtr->numPax,
				tPtr->outAirportID,
				tPtr->outAptICAO,
				tPtr->outFboID,
				tPtr->inAirportID,
				tPtr->inAptICAO,
				tPtr->inFboID,
				(tPtr->reqOut) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->reqOut))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				(tPtr->reqIn) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->reqIn))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				tPtr->contractFlag,
				tPtr->aircraftTypeID,
				tPtr->sequencePosn,
				tPtr->outCountryID,
				tPtr->inCountryID,
				tPtr->earlyAdj,
				tPtr->lateAdj,
				tPtr->noCharterFlag);
		}
		fprintf(logFile,"\
+--------+--------+-----+--------+--------+--------+--------+--------+--------+------------------+------------------+----------+----------+----------+-------+-------+-------+-------+-------|\n\n");
		fflush(logFile);
	}
	// free mysql results
	mysql_free_result(res);
	return(0);
}

/********************************************************************************
*	Function   readExclusionList              Date last modified:   /  /06 SWO
*	Purpose:  Read in Exclusion List from mySQL db.
********************************************************************************/
typedef enum {
	excType = 0, excFirstval, excSecondval, exc_end_of_list = 255
} excSqlColumns;
static int readExclusionList(MY_CONNECTION *myconn)
{
 /*
	TYPE					FIRSTID			SECONDID
	1 airport-curfew start	                airportID		curfewstarttime - minutes after midnight GMT
	2 airport-curfew end	                airportID		curfewendtime - minutes after midnight GMT
	3 aircraftType-airport	                aircraftTypeID	        airportID
	4 demand-aircraft		        demandID		aircraftID

	numExclusion = 5;

	if ((exclusionList = (Exclusion *)calloc(numExclusion,sizeof (Exclusion))) == NULL)
			error_handler("readExclusionList",COULDNT_ALLOCATE_MEMORY);
	exclusionList[0].exclusionID = 11;
	exclusionList[0].typeID = 1; //type 1 is airport curfew start
	exclusionList[0].firstID = 3453 ;//airportID for HPN
	exclusionList[0].secondID = 360; //curfew starttime

	exclusionList[1].exclusionID = 12;
	exclusionList[1].typeID = 2; //type 2 is airport curfew end
	exclusionList[1].firstID = 3453;//airportID
	exclusionList[1].secondID =660; //curfew endtime

	exclusionList[2].exclusionID = 13;
	exclusionList[2].typeID = 3; //type 3 is aircraftType - airport
	exclusionList[2].firstID = 11; //aircraftTypeID
	exclusionList[2].secondID = 3453; //airportID

	exclusionList[3].exclusionID = 14;
	exclusionList[3].typeID = 4; //type 4 is demand - aircraft
	exclusionList[3].firstID = 89809; //demandID
	exclusionList[3].secondID = 161; //aircraftID

	exclusionList[4].exclusionID = 15;
	exclusionList[4].typeID = 3; 
	exclusionList[4].firstID = 52;
	exclusionList[4].secondID = 1978; //airportID for BED
*/
	extern char *exclusionSQL;
	extern char *curfewexclusionSQL;
	BINTREENODE *tmp;
	char *sqlStmt = "select a.airportID, a.elevation, a.mainrunwaylength from tmp_apt t, airport a where a.icao = t.icao";
	double minRunwayLen, mainRunwayLen, elevation;
	AircraftType *atPtr;
	int atCount;
	Type_3_Exclusion_List *t3ptr;

	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	Exclusion *tPtr, *ePtr, exBuf;
	Demand dbuf;
	Airport abuf;

	int exc_type, first_value, second_value;
	
	if(!myDoQuery(myconn, exclusionSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	rowCount = mysql_num_rows(res);
	if(! rowCount)
		logMsg(logFile,"readExclusionList(): 0 rows returned.\n");
	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		exc_type = atoi(row[excType]);
		first_value = atoi(row[excFirstval]);
		second_value = atoi(row[excSecondval]);
		if(exc_type == Excln_Demand_Aircraft) {
			//demand-aircraft exclusion
			dbuf.demandID = first_value;
			if(!(TreeSearch(dmdXDemandIdRoot, &dbuf, demandIdCompare))) {
				// demandID in exclusion is not in planning window
				continue;
			}
		}
		if(exc_type == Excln_AircraftType_Airport) {
			abuf.airportID = second_value;
			if(!(TreeSearch(airportRoot, &abuf, airportCompare))) {
				// airportID in exclusion is not in list of airports
				continue;
			}
		}
		tPtr = calloc((size_t) 1, sizeof(Exclusion));
		if(! tPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readExclusionList().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		tPtr->typeID = exc_type;
		tPtr->firstID = first_value;
		tPtr->secondID = second_value;
		exclusionRoot = RBTreeInsert(exclusionRoot, tPtr, exclusionCompare);
		numOtherExclusion++;

	}
	// free mysql results
	mysql_free_result(res);

	if(!myDoQuery(myconn, curfewexclusionSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	rowCount = mysql_num_rows(res);
	if(! rowCount)
		logMsg(logFile,"readExclusionList(): 0 rows returned.\n");
	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		exc_type = atoi(row[excType]);
		first_value = atoi(row[excFirstval]);
		second_value = atoi(row[excSecondval]);
		tPtr = calloc((size_t) 1, sizeof(Exclusion));
		if(! tPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readExclusionList().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		tPtr->typeID = exc_type;
		tPtr->firstID = first_value;
		tPtr->secondID = second_value;
		curfewexclusionRoot = RBTreeInsert(curfewexclusionRoot, tPtr, curfewexclusionCompare);
		numCurfewExclusion++;

	}
	// free mysql results
	mysql_free_result(res);

	if(!myDoQuery(myconn, sqlStmt, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	rowCount = mysql_num_rows(res);
	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		elevation = atof(row[1]);
		mainRunwayLen = atof(row[2]);
		for(atCount = 0, atPtr = acTypeList; atCount < numAcTypes; ++atCount, ++atPtr) {
			minRunwayLen = minRunwayLength(wetDry, atPtr->aircraftTypeID, elevation);
			minRunwayLen = floor(minRunwayLen);
			if(minRunwayLen > mainRunwayLen) {
				exBuf.typeID = Excln_AircraftType_Airport; //aircrafttype-airport exclusion
				exBuf.firstID = atPtr->aircraftTypeID;
				exBuf.secondID = atoi(row[0]); // airportid
				if(! TreeSearch(exclusionRoot, &exBuf, exclusionCompare)) {
					tPtr = calloc((size_t) 1, sizeof(Exclusion));
					if(! tPtr) {
						logMsg(logFile,"%s Line %d, Out of Memory in readExclusionList().\n", __FILE__,__LINE__);
						writeWarningData(myconn); exit(1);
					}
					tPtr->typeID = exBuf.typeID;
					tPtr->firstID = exBuf.firstID;
					tPtr->secondID =  exBuf.secondID;
					exclusionRoot = RBTreeInsert(exclusionRoot, tPtr, exclusionCompare);
					numOtherExclusion++;
				}
			}
		}
	}
	mysql_free_result(res);

	// create exclusionList
	numExclusion = numOtherExclusion + numCurfewExclusion;
	exclusionList = (Exclusion *) calloc((size_t) numExclusion + 1, (size_t) sizeof(Exclusion));
	if(!exclusionList) {
		logMsg(logFile,"%s Line %d, Out of Memory in readExclusionList().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	for(tmp = Minimum(curfewexclusionRoot), ePtr = exclusionList; tmp; tmp = Successor(tmp), ePtr++) {
		tPtr = (Exclusion *) getTreeObject(tmp);
		ePtr->typeID = tPtr->typeID;
		ePtr->firstID = tPtr->firstID;
		ePtr->secondID = tPtr->secondID;
	}
	for(tmp = Minimum(exclusionRoot); tmp; tmp = Successor(tmp), ePtr++) {
		tPtr = (Exclusion *) getTreeObject(tmp);
		ePtr->typeID = tPtr->typeID;
		ePtr->firstID = tPtr->firstID;
		ePtr->secondID = tPtr->secondID;
	}
	if(!(type3exclusionList = (Type_3_Exclusion_List *) calloc((size_t) numAcTypes, sizeof(Type_3_Exclusion_List)))) {
		logMsg(logFile,"%s Line %d, Out of Memory in readExclusionList().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	for(atCount = 0, atPtr = acTypeList, t3ptr = type3exclusionList; atCount < numAcTypes; ++atCount, ++t3ptr, ++atPtr) {
		t3ptr->aircraftTypeID = atPtr->aircraftTypeID;
		for(tmp = Minimum(exclusionRoot); tmp; tmp = Successor(tmp)) {
			tPtr = (Exclusion *) getTreeObject(tmp);
			if(tPtr->typeID == Excln_AircraftType_Airport && tPtr->firstID == t3ptr->aircraftTypeID)
				t3ptr->count++;
		}
		if(t3ptr->count) {
			int idx = 0;
			t3ptr->airportIDptr = (int *) calloc((size_t) t3ptr->count + 1, sizeof(int));
			for(tmp = Minimum(exclusionRoot); tmp; tmp = Successor(tmp)) {
				tPtr = (Exclusion *) getTreeObject(tmp);
				if(tPtr->typeID == Excln_AircraftType_Airport && tPtr->firstID == t3ptr->aircraftTypeID) {
					*(t3ptr->airportIDptr + idx) = tPtr->secondID;
					++idx;
				}
			}
		}
	}

	if(verbose) {
		int x;

		logMsg(logFile,"exclusionList:\n");
		fprintf(logFile,"+--------+---------+----------+\n");
		fprintf(logFile,"| typeID | firstID | secondID |\n");
		fprintf(logFile,"+--------+---------+----------+\n");
		for(x = 0, tPtr = exclusionList; x < numExclusion; ++x, ++tPtr) {
			fprintf(logFile,"| %6d | %7d | %8d |\n", tPtr->typeID, tPtr->firstID, tPtr->secondID);
		}
		fprintf(logFile,"+--------+---------+----------+\n\n");

		logMsg(logFile,"excluded airports by aircraft type:\n");
		for(atCount = 0, t3ptr = type3exclusionList; atCount < numAcTypes; ++atCount, ++t3ptr) {
			fprintf(logFile,"airportIDs excluded for aircraftTypeID %d:\n", t3ptr->aircraftTypeID);
			for(x = 0; x < t3ptr->count; x++)
				fprintf(logFile,"%d\n", *(t3ptr->airportIDptr + x));
			fprintf(logFile,"\n");
		}
		fflush(logFile);
	}
	return 0;
}

typedef enum {
	aptAirportid = 0, aptIcao, apt_end_of_list = 255
} aptSqlColumns;
static int
getAirports(MY_CONNECTION *myconn)
{
	extern char *airportSQL;

	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	Airport *aPtr, aptBuf;
	char sqlBuf[256];
	BINTREENODE *tmp;
	
	if(!myDoQuery(myconn, airportSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"getAirports(): 0 rows returned.\n");
		return(0);
	}

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		if(! row[aptAirportid]) 
			continue;
		aptBuf.airportID = atoi(row[aptAirportid]);
		if(TreeSearch(airportRoot, &aptBuf, airportCompare))
			continue;
		aPtr = calloc((size_t) 1, sizeof(Airport));
		if(! aPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in getAirports().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		aPtr->airportID = aptBuf.airportID;
		strcpy(aPtr->ICAO, (row[aptIcao]) ? row[aptIcao] : "NULL");
		if(!(airportRoot = RBTreeInsert(airportRoot, aPtr, airportCompare))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in getAirports().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		numAirports++;
	}
	if(verbose) {
		BINTREENODE *tmp;
		logMsg(logFile,"airport list. %d airports\n", numAirports);
		for(tmp = Minimum(airportRoot); tmp; tmp = Successor(tmp)) {
			aPtr = (Airport *) getTreeObject(tmp);
			fprintf(logFile,"%5s %d\n", aPtr->ICAO, aPtr->airportID); 
		}
		fprintf(logFile,"\n");
		fflush(logFile);
	}

	if(!(myDoWrite(myconn,"create temporary table if not exists tmp_apt (icao varchar(8) not null primary key)"))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	if(!(myDoWrite(myconn,"delete from tmp_apt"))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	for(tmp = Minimum(airportRoot); tmp; tmp = Successor(tmp)) {
		aPtr = (Airport *) getTreeObject(tmp);
		sprintf(sqlBuf,"insert into tmp_apt values('%s')", aPtr->ICAO);
		if(!(myDoWrite(myconn,sqlBuf))) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
			writeWarningData(myconn); exit(1);
		}
	}
	mysql_free_result(res);
	return(0);
}

static int
peakDayContractRatesCompare(void *a, void *b)
{
	PeakDayContractRate *a1 = (PeakDayContractRate *) a;
	PeakDayContractRate *b1 = (PeakDayContractRate *) b;
	int ret;

	if((ret = a1->contractid - b1->contractid))
		return(ret);
	return(a1->level_id - b1->level_id);
}

static int
peakDayCompare(void *a, void *b)
{
	PeakDay *a1 = (PeakDay *) a;
	PeakDay *b1 = (PeakDay *) b;

	return(a1->peak_day - b1->peak_day);
}

static int
airportCompare(void *a, void *b)
{
	Airport *a1 = (Airport *) a;
	Airport *b1 = (Airport *) b;
	return(a1->airportID - b1->airportID);
}

static int
rawAircraftCompare(void *a, void *b)
{
	int ret;
	RawAircraftData *a1 = (RawAircraftData *) a;
	RawAircraftData *b1 = (RawAircraftData *) b;

	ret = a1->aircraftid - b1->aircraftid;
	if(ret)
		return(ret);
	if(a1->rec_outtime > b1->rec_outtime)
		return(1);
	if(a1->rec_outtime < b1->rec_outtime)
		return(-1);
	return(0);
}

static int
flownAircraftCompare(void *a, void *b)
{
	int ret;
	RawAircraftData *a1 = (RawAircraftData *) a;
	RawAircraftData *b1 = (RawAircraftData *) b;

	if(a1->rec_outtime > b1->rec_outtime)
		return(1);
	if(a1->rec_outtime < b1->rec_outtime)
		return(-1);
	ret = a1->aircraftid - b1->aircraftid;
	if(ret)
		return(ret);
	return(0);
}

static int
demandIdCompare(void *a, void *b)
{
	Demand *a1 = (Demand *) a;
	Demand *b1 = (Demand *) b;
	return(a1->demandID - b1->demandID);
}

static int
exclusionCompare(void *a, void *b)
{
	Exclusion *a1 = (Exclusion *) a;
	Exclusion *b1 = (Exclusion *) b;
	int ret;

	if((ret = a1->typeID - b1->typeID))
		return(ret);
	else if((ret = a1->firstID - b1->firstID))
		return(ret);
	else
		return(a1->secondID - b1->secondID);
}

static int
curfewexclusionCompare(void *a, void *b)
{
	Exclusion *a1 = (Exclusion *) a;
	Exclusion *b1 = (Exclusion *) b;
	int ret;

	if((ret = a1->typeID - b1->typeID))
		return(ret);
	else
		return(a1->firstID - b1->firstID);
}

#ifdef DEBUGGING
static void
displayResults(MYSQL_RES *res, MYSQL_FIELD *colInfo)
{
	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	int num_cols;
	int *fieldLengths;
	int i, x;
	char tbuf[32];

	num_cols = mysql_num_fields(res);
	rowCount = mysql_num_rows(res);
	fieldLengths = (int *) calloc((size_t) num_cols + 1, sizeof(int));

	for(i = 0; i < num_cols; i++) {
		x = (int) strlen((colInfo + i)->name);
		*(fieldLengths + i) = (x > (int) (colInfo + i)->max_length) ? x : (colInfo + i)->max_length;
		if(*(fieldLengths + i) < 4)
			*(fieldLengths + i) = 4;
	}

	// do the "+-------+-------+-----------------+" line
	fprintf(logFile,"+");
	for(i = 0; i < num_cols; i++) {
		for(x = 0; x < *(fieldLengths + i) + 2; ++x)
			fprintf(logFile,"-");
		fprintf(logFile,"+");
	}
	fprintf(logFile,"\n");

	// do the "| fname | fname | fname           |" line
	fprintf(logFile,"|");
	for(i = 0; i < num_cols; i++) {
		sprintf(tbuf," %%%s%ds |", IS_NUM((colInfo + i)->type) ? "" : "-", *(fieldLengths + i));
		fprintf(logFile,tbuf, (colInfo + i)->name);
	}
	fprintf(logFile,"\n");

	// do another "+-------+-------+-----------------+" line
	fprintf(logFile,"+");
	for(i = 0; i < num_cols; i++) {
		for(x = 0; x < *(fieldLengths + i) + 2; ++x)
			fprintf(logFile,"-");
		fprintf(logFile,"+");
	}
	fprintf(logFile,"\n");

	// print the column values
	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		fprintf(logFile,"|");
		for(i = 0; i < num_cols; i++) {
			sprintf(tbuf," %%%s%ds |", IS_NUM((colInfo + i)->type) ? "" : "-", *(fieldLengths + i));
			fprintf(logFile,tbuf, row[i] ? row[i] : "NULL");
		}
		fprintf(logFile,"\n");
	}

	// do another "+-------+-------+-----------------+" line
	fprintf(logFile,"+");
	for(i = 0; i < num_cols; i++) {
		for(x = 0; x < *(fieldLengths + i) + 2; ++x)
			fprintf(logFile,"-");
		fprintf(logFile,"+");
	}
	fprintf(logFile,"\n");

	free((void *) fieldLengths);
}
#endif // DEBUGGING

//////////////////////////////////////////////////////////////////////
// crew data collection
//////////////////////////////////////////////////////////////////////
static int
readCrewList(MY_CONNECTION *myconn)
{
	int errNbr1, errNbr2, x;
	MYSTACK *stack;
	CrewPairX *pxPtr, *pxPtr0, pxBuf;
	QITEM *qi;
	LookupRet lkRet; //lkRet1;
	char *arg;
	int fakeCrewID;
	int recNotInSS, notRecNotInSS;
	DateTime tmpDate, tmpDateE; //, tmpDate1, tmpDate2;
	time_t tmpDate_t;
	char curcrew[32];
	int pre_crewCount;
	int crewCount;
	PRE_Crew *preCrewPtr;
	CS_CrewData *cs_cdPtr, cs_cdBuf;
	char lbuf[4096], tbuf1[32], tbuf2[32], txbuf[128]; //, tbuf3[32], tbuf4[32];
	char *wptrs[128];
	int wc, ret;
	CrewData *cdPtr, *cdPtr1, *cdPtr2, cdBuf;
	CsTravelData *cstrlPtr, *cstrlBuf;
	CrewID *cdidPtr, cdidBuf;
	Crew *cPtr; //, *c1Ptr, crewBuf;
	SS_CrewData *ssCdPtr, *ssCdPtr0, ssCdBuf; //*nextSsCdPtr
	BINTREENODE *tmp, *tmp1, *tmp2, *oldTree; //, *tmp3, *tmp4;
	PRE_Crew_Pair *preCrewPairPtr, preCrewPairBuf;
	CrewPair *cpPtr, *cpPtr1; //, cpBuf;
	Leg *lPtr;
	int tmpDays;
	DateTime tmpTourEndTm;
	AircraftCrewPairXref *acpxPtr, acpxBuf;
	int timeZoneByAirport = 0;
	bool SSrecFlag = 0;
    char writetodbstring1[200];
    int days, hours, minutes, seconds, msecs;
	Apt_ICAOAptID *allPtr;// for startLoc and endLoc, RLZ 09/15/2008
	int timeZoneDiff; //for ajusting the tourStart and tourEnd if startLoc, endLoc is different from base
	RawAircraftData *radLastLegPtr;

//	char txbuf[128];

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////
	pwStartDate = dt_time_tToDateTime(optParam.windowStart);
	pwEndDate = dt_time_tToDateTime(optParam.windowEnd);
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////


	///////////////////////////
	// combine bw and ss data //
	///////////////////////////
	/*
	if(verbose) showCrewDataNotInSsHeader("Bitwise Crew Assignment Records for which there is no corresponding Schedule Soft record");
	for(tmp = Minimum(empnbrStarttimeActypRegRoot); tmp; tmp = Successor(tmp)) {
		cdPtr = (CrewData *) getTreeObject(tmp);
		ssCdBuf.zbadgeid = cdPtr->employeenumber;
		ssCdBuf.zdeptdesc = cdPtr->aircraftTypeName;
		tmpDate = cdPtr->starttime; //tmpDate = gmtDayToSSday(cdPtr->starttime);  // *** locFromGMTinMinutes? same for every crew???
		//for(; tmpDate < cdPtr->endtime || gmtDayInSSdayRange(tmpDate,cdPtr->endtime,tmpDate); tmpDate = dt_addToDateTime(Hours, 24, tmpDate)) {
		for(; SSdayInRange(cdPtr->starttime, tmpDate,cdPtr->endtime)
			 && (dt_addToDateTime(Hours, - 14, cdPtr->endtime) & DateOnly) >= tmpDate; 
			tmpDate = dt_addToDateTime(Hours, 24, tmpDate)) 
		{ // debug experimental
			ssCdBuf.dtdate = tmpDate;
			tmp1 = TreeSearch(badgeDeptDateRoot, &ssCdBuf, badgeDeptDateCmp);
			if(! tmp1) {
				if(TreeSearch(dateListRoot, &(ssCdBuf.dtdate), dateListCmp)) {
					if(verbose) showCrewDataNotInSs(cdPtr, ssCdBuf.dtdate);
					//*** WARNING MSG

					// create pseudo schedule soft record
					ssCdPtr = crewDataToSS_CrewData(ssCdBuf.dtdate, cdPtr);

					// associate bitwise record with schedule soft record
					if(!(ssCdPtr->bwWorkRoot = RBTreeInsert(ssCdPtr->bwWorkRoot, cdPtr, bwWorkCmp))) {
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn); exit(1);
					}

					// add pseudo record to schedule soft data
					if(!(badgeDeptDateRoot = RBTreeInsert(badgeDeptDateRoot, ssCdPtr, badgeDeptDateCmp))) {
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn); exit(1);
					}
				}
				continue;
			}
			ssCdPtr = (SS_CrewData *) getTreeObject(tmp1);
			if(!(ssCdPtr->bwWorkRoot = RBTreeInsert(ssCdPtr->bwWorkRoot, cdPtr, bwWorkCmp))) {
				logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
	}
	if(verbose) showCrewDataNotInSsFooter();
*/
	///////////////////////////
	// combine bw and ss data: rewrote to accomodate the day by day crew assignment. 
	// History: 10/05/07 RLZ
	///////////////////////////
	if(verbose) showCrewDataNotInSsHeader("Bitwise Crew Assignment Records for which there is no corresponding Schedule Soft record");
	for(tmp = Minimum(empnbrStarttimeActypRegRoot); tmp; tmp = Successor(tmp)) {
		SSrecFlag = 0;
		cdPtr = (CrewData *) getTreeObject(tmp);
		ssCdBuf.zbadgeid = cdPtr->employeenumber;
		ssCdBuf.zdeptdesc = cdPtr->aircraftTypeName;
		tmpDate = cdPtr->starttime; 
		//ssCdBuf.dtdate = tmpDate;
		//tmp1 = TreeSearch(badgeDeptDateRoot, &ssCdBuf, badgeDeptDateCmp);
		ssCdBuf.dtdate = (DateTime) 0;
		lkRet = Lookup(badgeDeptDateRoot, &ssCdBuf, badgeDeptDateCmp, &tmp1);
		switch(lkRet) {
		case EmptyTree:
		case GreaterThanLastItem:
		case ExactMatchFound: // impossible, since we don't enter a dept or date
			logMsg(logFile,"%s Line %d: employeenumberr %s not found.\n", __FILE__,__LINE__, cdPtr->employeenumber);
			sprintf(writetodbstring1, "%s Line %d: employeenumberr %s not found.", __FILE__,__LINE__, cdPtr->employeenumber);
		    if(errorNumber==0)
			{  if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		         {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		          writeWarningData(myconn); exit(1);
	             }
			}				
			else
			{  if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		         {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		          writeWarningData(myconn); exit(1);
	             } 
			}
			initializeWarningInfo(&errorinfoList[errorNumber]);
		    errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
            strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
		    errorinfoList[errorNumber].crewid=cdPtr->crewid;
		    sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
			sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			errorinfoList[errorNumber].format_number=24;
            strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
		    errorNumber++;
			continue;
			break;
		case NotFoundReturnedNextItem:
			for(; tmp1; tmp1 = Successor(tmp1)) 
			{
				ssCdPtr = (SS_CrewData *) getTreeObject(tmp1);
				if(strcmp(cdPtr->employeenumber, ssCdPtr->zbadgeid) == 0 )
				  {	if (tmpDate >= dt_addToDateTime(Hours, -1, ssCdPtr->dtdate) && tmpDate <= dt_addToDateTime(Hours, 24, ssCdPtr->dtdate)) 
				    { // -1: one hour is a tolerance in the current schedule. so that we do not need to create pseudo ssoft record. RLZ 11/19/2007
						oldTree = ssCdPtr->bwWorkRoot;
						if(!(ssCdPtr->bwWorkRoot = RBTreeInsert(ssCdPtr->bwWorkRoot, cdPtr, bwWorkCmp))) 
						{
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
						ssCdPtr->bwWorkRoot = oldTree;
						continue;
						//writeWarningData(myconn); 
						//exit(1);
						}
						strcpy(txbuf, returnUpper(ssCdPtr->zpostdesc));
						if(strstr(txbuf,"TRAVEL") || strstr(txbuf,"TRAINING")) //We may want to remove this if-statement, RLZ. 03032008
						{   // find crew assignment on the TRAVEL day, make it available. RLZ
								ssCdPtr->zlname = "Rec not in SS";
								ssCdPtr->zpostdesc = "Unrestricted";
								fprintf(logFile,"Warning: Crew %s is assigned on TRAVEL day %s\n", cdPtr->fileas, dt_DateTimeToDateTimeString(tmpDate, tbuf1, "%Y/%m/%d %H:%M"));
								sprintf(writetodbstring1, "Warning: Crew %s is assigned on TRAVEL/TRAINING day %s", cdPtr->fileas, dt_DateTimeToDateTimeString(tmpDate, tbuf1, "%Y/%m/%d %H:%M")); 
                           if(errorNumber==0)
						    {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                       {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		                        writeWarningData(myconn); exit(1);
						       }
						    }
						   else
						    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                       {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		                        writeWarningData(myconn); exit(1);
	                           }
						    }
						    initializeWarningInfo(&errorinfoList[errorNumber]);
							errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                            sprintf(errorinfoList[errorNumber].group_name,"%s", "group_crew");
						    errorinfoList[errorNumber].crewid = cdPtr->crewid;
						    sprintf(errorinfoList[errorNumber].datatime_str, "%s", dt_DateTimeToDateTimeString(tmpDate, tbuf1, "%Y/%m/%d %H:%M"));
							errorinfoList[errorNumber].format_number=1;
                            sprintf(errorinfoList[errorNumber].notes,"%s", writetodbstring1);
							errorNumber++;   
						}
						SSrecFlag = 1;
						break;
				    }	
				   else
				      continue;
				}
				else
					break;
			}
			break;
		}
		
		if (!SSrecFlag){// Create a pesudo schedule soft data....
			fprintf(logFile,"BW CrewAssignmentID: %5d, not scheduled in scheduleSoft \n", cdPtr->crewassignmentid );
			sprintf(writetodbstring1, "BW CrewAssignmentID: %5d, not scheduled in scheduleSoft", cdPtr->crewassignmentid);
            if(errorNumber==0)
	          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		          {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		           writeWarningData(myconn); exit(1);
				  }
			  }
			else
			  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		          {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		           writeWarningData(myconn); exit(1);
	              }
			  }
			initializeWarningInfo(&errorinfoList[errorNumber]);
			errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
            strcpy(errorinfoList[errorNumber].group_name,"group_crewassg");
			errorinfoList[errorNumber].crewassgid=cdPtr->crewassignmentid;
			errorinfoList[errorNumber].format_number=2;
            strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
			errorNumber++;
			// if(TreeSearch(dateListRoot, &(ssCdBuf.dtdate), dateListCmp)) {  //So we will not use dateListRoot any more. RLZ
			if(verbose) showCrewDataNotInSs(cdPtr, tmpDate); // ssCdBuf.dtdate);
			//*** WARNING MSG
			if (tmp1){
				ssCdPtr0 = (SS_CrewData *) getTreeObject(Predecessor(tmp1));
				if(strcmp(cdPtr->employeenumber, ssCdPtr0->zbadgeid) == 0 ){
					ssCdPtr = crewDataToSS_CrewData(ssCdPtr0, cdPtr); // create pseudo schedule soft record
				// associate bitwise record with schedule soft record
					oldTree = ssCdPtr->bwWorkRoot; 
					if(!(ssCdPtr->bwWorkRoot = RBTreeInsert(ssCdPtr->bwWorkRoot, cdPtr, bwWorkCmp))) {
							logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
							writeWarningData(myconn); 
							ssCdPtr->bwWorkRoot = oldTree;
							continue;
							//exit(1);
					}

					// add pseudo record to schedule soft data
					oldTree = badgeDeptDateRoot;
					if(!(badgeDeptDateRoot = RBTreeInsert(badgeDeptDateRoot, ssCdPtr, badgeDeptDateCmp))) {
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn); 
						badgeDeptDateRoot = oldTree;
						continue;
						//exit(1);
					}
				}
				else 
				{fprintf(logFile,"No pseudo ssoftcrew record was created (0)\n");
				 sprintf(writetodbstring1, "No pseudo ssoftcrew record was created (0)");
				 if(errorNumber==0)
	                {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
				       }
			        }
			     else
			       {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			       }
				   initializeWarningInfo(&errorinfoList[errorNumber]);
		           errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
			       errorinfoList[errorNumber].format_number=3;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
		           errorNumber++;   
				}
			}			
			else 
			{ fprintf(logFile,"No pseudo ssoftcrew record was created (1)\n");
			  sprintf(writetodbstring1, "No pseudo ssoftcrew record was created (1)");
              if(errorNumber==0)
	            {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
				       }
			    }
			  else
			    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			    }
			    initializeWarningInfo(&errorinfoList[errorNumber]);
		        errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
			    errorinfoList[errorNumber].format_number=4;
                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
		        errorNumber++;  
			}
		}
	}
	if(verbose) showCrewDataNotInSsFooter();

	///////////////////////////////////////////////////////
	// display combined data and get count of crew members
	///////////////////////////////////////////////////////
	curcrew[0] = '\0';
	pre_crewCount = 0;
	if(verbose) fprintf(logFile,"%s\n\n", tabmsg);
	if(verbose) showCombined_CrewDataHeader("Combined Schedule Soft/Bitwise Data");
	for(tmp = Minimum(badgeDeptDateRoot); tmp; tmp = Successor(tmp)) {
		ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
		if(verbose) showCombined_CrewData(ssCdPtr);
		if(strcasecmp(curcrew, ssCdPtr->zbadgeid) != 0) {
			++pre_crewCount;
			strcpy(curcrew, ssCdPtr->zbadgeid);
		}
	}
	if(verbose) showCombined_CrewDataFooter((char *) 0);

	///////////////////////////
	// initialize pre-crewList
	///////////////////////////
	// allocate one PRE_Crew structure per pilot
	if(!(preCrewList = (PRE_Crew *) calloc((size_t) pre_crewCount + 1, sizeof(PRE_Crew)))) {
		logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	preCrewPtr = preCrewList;
	curcrew[0] = '\0';
	fakeCrewID = 1000000; // use this if there were no crewassignment records 
	//*** We should skip this one completely, and print a warning message instead. Because this fake crew will get a airportid = 0;

	for(tmp = Minimum(badgeDeptDateRoot); tmp; tmp = Successor(tmp)) {
		ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
		if(strcasecmp(curcrew, ssCdPtr->zbadgeid) != 0) {
			strcpy(curcrew, ssCdPtr->zbadgeid);
			preCrewPtr->employeenumber = strdup(ssCdPtr->zbadgeid);
			cdidBuf.employeenumber = curcrew;
			tmp1 = TreeSearch(empnbrRoot, &cdidBuf, empnbrCmp);
			if(tmp1) {
				cdidPtr = getTreeObject(tmp1);
				preCrewPtr->crewID = cdidPtr->crewid;
				preCrewPtr->startLoc = cdidPtr->baseairportid;
				preCrewPtr->endLoc = cdidPtr->baseairportid;
				preCrewPtr->baseairportid = cdidPtr->baseairportid;
				preCrewPtr++;
				continue;
			}
			if(ssCdPtr->bwWorkRoot) {
				cdPtr = getTreeObject(ssCdPtr->bwWorkRoot);
				preCrewPtr->crewID = cdPtr->crewid;
				preCrewPtr++;
				continue;
			}
			for(Successor(tmp); tmp; tmp = Successor(tmp)) {
				ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
				if(strcasecmp(curcrew, ssCdPtr->zbadgeid) == 0) {
					if(ssCdPtr->bwWorkRoot) {
						cdPtr = getTreeObject(ssCdPtr->bwWorkRoot);
						preCrewPtr->crewID = cdPtr->crewid;
						break;
					}
				}
				else {
					tmp = Predecessor(tmp);
					ssCdPtr = (SS_CrewData *) getTreeObject(Successor(tmp));
					preCrewPtr->crewID = fakeCrewID++;
					break;
				}
			}
			if(! tmp)
				preCrewPtr->crewID = fakeCrewID++;
			preCrewPtr++;
		}
	}

// debug ////////////////////////////////////////////////////////////////////////////////
//	if(verbose) {
//		fprintf(logFile,"\n\n\n");
//		preCrewPtr = preCrewList;
//		while(preCrewPtr->employeenumber) {
//			fprintf(logFile,"%6s %d\n", preCrewPtr->employeenumber, preCrewPtr->crewID);
//			++preCrewPtr;
//		}
//		fprintf(logFile,"\n\n\n");
//	}
// end debug ////////////////////////////////////////////////////////////////////////////

	// link associated records to each PRE_Crew structure through PRE_Crew.ssCrewDataRoot
	preCrewPtr = preCrewList;
	while(preCrewPtr->employeenumber) {
		ssCdBuf.zbadgeid = preCrewPtr->employeenumber;
		ssCdBuf.zdeptdesc = "";
		ssCdBuf.dtdate = (DateTime) 0;
		lkRet = Lookup(badgeDeptDateRoot, &ssCdBuf, badgeDeptDateCmp, &tmp);
		switch(lkRet) {
		case EmptyTree:
		case GreaterThanLastItem:
		case ExactMatchFound: // impossible, since we don't enter a dept or date
			logMsg(logFile,"%s Line %d: employeenumberr %s not found.\n", __FILE__,__LINE__, preCrewPtr->employeenumber);
			sprintf(writetodbstring1, "%s Line %d: employeenumberr %s not found.", __FILE__,__LINE__, preCrewPtr->employeenumber);
			if(errorNumber==0)
	            {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
				       }
			    }
			else
			    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			    }
			initializeWarningInfo(&errorinfoList[errorNumber]);
			errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
            strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
		    errorinfoList[errorNumber].crewid=preCrewPtr->crewID;
		    sprintf(errorinfoList[errorNumber].filename, "%s", __FILE__);
		    sprintf(errorinfoList[errorNumber].line_number,"%d",__LINE__);
			errorinfoList[errorNumber].format_number=24;
            strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
			errorNumber++;
			++preCrewPtr;
			continue;
			break;
		case NotFoundReturnedNextItem:
			for(recNotInSS = 0, notRecNotInSS = 0; tmp; tmp = Successor(tmp)) {
				ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
				if(strcmp(preCrewPtr->employeenumber, ssCdPtr->zbadgeid) == 0) {
					// add to PRE_CREW.ssCrewDataRoot
					oldTree = preCrewPtr->ssCrewDataRoot;
					if(!(preCrewPtr->ssCrewDataRoot = RBTreeInsert(preCrewPtr->ssCrewDataRoot, ssCdPtr, badgeDeptDateCmp))) {
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn);
						preCrewPtr->ssCrewDataRoot = oldTree;
						continue;
						//exit(1);
					}

					// mark available during planning window if this is the case
					if(SSdayInRange(pwStartDate,ssCdPtr->dtdate,pwEndDate)) {
						strcpy(lbuf, returnUpper(ssCdPtr->zpostdesc));
						if(!cantFly(lbuf))
							preCrewPtr->availDuringPlanningWindow++;
						else {
							// count how many of these are "not in ss" records and how many are not
							ret = strcasecmp(lbuf,"REC NOT IN SS");
							if(ret == 0)
								++recNotInSS;
							else
								++notRecNotInSS;
						}
					}
				}
				else
					break;
			}
			// if only records we had were Bitwise crewassignment records in the date range, then say this pilot is available
			if(recNotInSS && ! notRecNotInSS)
				preCrewPtr->availDuringPlanningWindow++;
			break;
		}
		++preCrewPtr;
	}
 
	// Make another pass through preCrewList to figure out position and aircraftType.
	// and to get Crew data structure count.
	// add in cs_crew_duty data
	preCrewPtr = preCrewList;
	while(preCrewPtr->employeenumber) {
		if(preCrewPtr->availDuringPlanningWindow == 0) {
			++preCrewPtr;
			continue;
		}


		// add in bw_crew_duty table data
		// Establish crewassignment list for every precrew pointer: Jintao: 10/01/2007
		memset(&cdBuf, '\0', sizeof(cdBuf));
		cdBuf.crewid = preCrewPtr->crewID;
		lkRet = Lookup(crewidStarttimeEndtimeAcidRoot, &cdBuf, crewidStarttimeEndtimeAcidCmp, &tmp);
		switch(lkRet) {
		case EmptyTree:
		case GreaterThanLastItem:
		case ExactMatchFound: // impossible, since we just entered crewid
			logMsg(logFile,"%s Line %d: crewid %d not found in bw_crew_duty data. lkRet=%d\n", __FILE__,__LINE__, preCrewPtr->crewID, lkRet);
			break;
		case NotFoundReturnedNextItem:
			for(; tmp; tmp = Successor(tmp)) {
				cdPtr = (CrewData *) getTreeObject(tmp);
				if(cdPtr->crewid != preCrewPtr->crewID)
					break;
				oldTree = preCrewPtr->bwCrewAssgRoot;
				if(!(preCrewPtr->bwCrewAssgRoot = RBTreeInsert(preCrewPtr->bwCrewAssgRoot, cdPtr, crewidStarttimeEndtimeAcidCmp))) {
					logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
					writeWarningData(myconn); 
					preCrewPtr->bwCrewAssgRoot = oldTree;
					continue;
					//exit(1);
				}
			}
			break;
		}


		// add in cs_crew_duty table data
		memset(&cs_cdBuf, '\0', sizeof(cs_cdBuf));
		cs_cdBuf.crewid = preCrewPtr->crewID;
		lkRet = Lookup(cs_crew_dataRoot, &cs_cdBuf, cs_crew_data_crewidScheduledOnStarttimeCmp, &tmp);
		switch(lkRet) {
		case EmptyTree:
		case GreaterThanLastItem:
		case ExactMatchFound: // impossible, since we just entered crewid
			logMsg(logFile,"%s Line %d: crewid %d not found in cs_crew_duty data. lkRet=%d\n", __FILE__,__LINE__, preCrewPtr->crewID, lkRet);
			break;
		case NotFoundReturnedNextItem:
			for(; tmp; tmp = Successor(tmp)) {
				cs_cdPtr = (CS_CrewData *) getTreeObject(tmp);
				if(cs_cdPtr->crewid != preCrewPtr->crewID)
					break;
				oldTree = preCrewPtr->csCrewDataRoot;
				if(!(preCrewPtr->csCrewDataRoot = RBTreeInsert(preCrewPtr->csCrewDataRoot, cs_cdPtr, cs_crew_data_crewidScheduledOnStarttimeCmp))) {
					logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
					writeWarningData(myconn); 
					preCrewPtr->csCrewDataRoot = oldTree;
					continue;
					//exit(1);
				}
			}
			break;
		}

		// set up queues
		preCrewPtr->positionList = createQueue();
		preCrewPtr->aircraftTypeList = createQueue();

		for(tmp = Minimum(preCrewPtr->ssCrewDataRoot); tmp; tmp = Successor(tmp)) {
			ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
			strcpy(lbuf, returnUpper(ssCdPtr->zpostdesc));
			if((!cantFly(lbuf)) && SSdayInRange(pwStartDate, ssCdPtr->dtdate, pwEndDate)) {
				if(strcasecmp(ssCdPtr->zlname,"Rec not in SS") != 0) { // skip pseudo schedule soft records here
					// add position to list of positions
					if(!preCrewPtr->positionList)
						QAddToTail(preCrewPtr->positionList, ssCdPtr->zshiftdesc);
					else {
						for(qi = QGetHead(preCrewPtr->positionList); qi; qi = QGetNext(preCrewPtr->positionList, qi)) {
							if(strcmp((char *) QGetObject(qi), ssCdPtr->zshiftdesc) == 0)
								break;
						}
						if(! qi) // zshiftdesc wasn't found
							QAddToTail(preCrewPtr->positionList, ssCdPtr->zshiftdesc);
					}
		
					// add aircraftType to list of aircraftTypes
					if(!preCrewPtr->aircraftTypeList)
						QAddToTail(preCrewPtr->aircraftTypeList, ssCdPtr->zdeptdesc);
					else {
						for(qi = QGetHead(preCrewPtr->aircraftTypeList); qi; qi = QGetNext(preCrewPtr->aircraftTypeList, qi)) {
							if(strcmp((char *) QGetObject(qi), ssCdPtr->zdeptdesc) == 0)
								break;
						}
						if(! qi) // zdeptdesc wasn't found
							QAddToTail(preCrewPtr->aircraftTypeList, ssCdPtr->zdeptdesc);
					}
				}
			}
		}
		++preCrewPtr;
	}

	

	// display, fix problems with positions and aircraft types
	preCrewPtr = preCrewList;
	while(preCrewPtr->employeenumber) {
		if(preCrewPtr->availDuringPlanningWindow == 0) {
			++preCrewPtr;
			continue;
		}

		// show multiple positions
		if((ret = QGetCount(preCrewPtr->positionList)) > 1) {
			fprintf(logFile,"%s:", preCrewPtr->employeenumber);
			for(qi = QGetHead(preCrewPtr->positionList); qi; qi = QGetNext(preCrewPtr->positionList, qi))
				fprintf(logFile," %s", (char *) QGetObject(qi));
			fprintf(logFile,"\n");
			preCrewPtr->position = analysePositions(preCrewPtr);
		}
		else if(ret == 0) {
			fprintf(logFile,"%s: can't determine position from Schedule Soft data.\n", preCrewPtr->employeenumber);
			sprintf(writetodbstring1, "%s: can't determine position from Schedule Soft data.", preCrewPtr->employeenumber);
			if(errorNumber==0)
	            {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
				       }
			    }
			else
			    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			    }
			initializeWarningInfo(&errorinfoList[errorNumber]);
            errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
            strcpy(errorinfoList[errorNumber].group_name,"group_crew");
		    errorinfoList[errorNumber].crewid=preCrewPtr->crewID;
			errorinfoList[errorNumber].format_number=5;
            strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
			errorNumber++;
			preCrewPtr->position = analysePositions(preCrewPtr);
		}
		else {
			if(strcasecmp((char *) QGetObject(QGetHead(preCrewPtr->positionList)), "PIC") == 0)
				preCrewPtr->position = 1;
			else if(strcasecmp((char *) QGetObject(QGetHead(preCrewPtr->positionList)), "SIC") == 0)
				preCrewPtr->position = 2;
			else
				preCrewPtr->position = 0;
		}

		// show multiple aircraft Types
		if((ret = QGetCount(preCrewPtr->aircraftTypeList)) > 1) {
			fprintf(logFile,"%s:", preCrewPtr->employeenumber);
			for(qi = QGetHead(preCrewPtr->aircraftTypeList); qi; qi = QGetNext(preCrewPtr->aircraftTypeList, qi))
				fprintf(logFile," %s", (char *) QGetObject(qi));
			fprintf(logFile,"\n");
			preCrewPtr->aircraftTypeID = analyseAircraftType(preCrewPtr);
		}
		else if(ret == 0) {
			fprintf(logFile,"%s: can't determine aircraftType from Schedule Soft data.\n", preCrewPtr->employeenumber);
			sprintf(writetodbstring1, "%s: can't determine aircraftType from Schedule Soft data.", preCrewPtr->employeenumber);
			if(errorNumber==0)
	            {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
				       }
			    }
			else
			    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			    }
			initializeWarningInfo(&errorinfoList[errorNumber]);
			errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
            strcpy(errorinfoList[errorNumber].group_name,"group_crew");
		    errorinfoList[errorNumber].crewid = preCrewPtr->crewID;
			errorinfoList[errorNumber].format_number=6;
            strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
			errorNumber++;
			preCrewPtr->aircraftTypeID = analyseAircraftType(preCrewPtr);
		}
		else {
			arg = QGetObject(QGetHead(preCrewPtr->aircraftTypeList));
			preCrewPtr->aircraftTypeID = getAcTypeID(arg);
		}
		++preCrewPtr;
	}

	if(verbose && verbose1) {
		showCombined_CrewDataHeader("Not Flying: Combined Schedule Soft/Bitwise Data");
		preCrewPtr = preCrewList;
		while(preCrewPtr->employeenumber) {
			if(preCrewPtr->availDuringPlanningWindow > 0) {
				++preCrewPtr;
				continue;
			}
			for(tmp = Minimum(preCrewPtr->ssCrewDataRoot); tmp; tmp = Successor(tmp)) {
				ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
				if(SSdayInRange(pwStartDate, ssCdPtr->dtdate, pwEndDate))
					showCombined_CrewData(ssCdPtr);
			}
			++preCrewPtr;
		}
		showCombined_CrewDataFooter((char *) 0);
	}

	// figure out:
	// - tour start time
	// - tour end time
	// - start training location
	// - end training location
	preCrewPtr = preCrewList;
	while(preCrewPtr->employeenumber){
		if(preCrewPtr->availDuringPlanningWindow == 0) {
			++preCrewPtr;
			continue;
		}


		getTourStartEnd(preCrewPtr);
		if(preCrewPtr->availDuringPlanningWindow == 0) {
			showCombined_CrewDataHeader("Unable to determine one of tourStartTm or tourEndTm");
			for(tmp = Minimum(preCrewPtr->ssCrewDataRoot); tmp; tmp = Successor(tmp)) {
				ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
				showCombined_CrewData(ssCdPtr);
			}
			showCombined_CrewDataFooter((char *) 0);
			++preCrewPtr;
			continue;
		}
		if( hasAlreadyFlown(preCrewPtr))
			preCrewPtr->hasFlown = 1;

		//# of days early pilot is willing to start before tour starts
		for(tmp = Minimum(preCrewPtr->ssCrewDataRoot); tmp; tmp = Successor(tmp)) {
			ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
			if( ssCdPtr->dtdate  == preCrewPtr->tourStartTm ){ //& DateOnly))
			//Find the temp start Loc from ssoft: RLZ 09/15/2008
				if(ssCdPtr->znote) {
					if(strlen(ssCdPtr->znote)> 10){ //exclude short notes
						strcpy(txbuf, returnUpper(ssCdPtr->znote));
						wc = split(txbuf, " ", wptrs);
						if(wc >= 4 && strcmp(wptrs[0],"BEGIN") == 0 && (strcmp(wptrs[1],"TOUR") == 0 && strcmp(wptrs[2],"FROM") == 0)) {
							//allPtr = getAirportLatLonInfoByIcao(wptrs[2]);
							allPtr = getAirportInfoByIcao(wptrs[3]);
							if(allPtr)
								preCrewPtr->startLoc = allPtr->airportID;
						}
					}	
				}
				break;
			}
		}
		if(tmp) {
			int daysFromStart = 1;
			//AirportLatLon *allPtr;
			//Apt_ICAOAptID *allPtr;// Jintao's change
			//char txbuf[128];
			if(strlen(ssCdPtr->zacccodeid) == 0) {
				// back up counting OT days
				// while previous record date is current date -1
				tmpDate = dt_addToDateTime(Hours, -24, ssCdPtr->dtdate);
				for(tmp = Predecessor(tmp); tmp; tmp = Predecessor(tmp), ++daysFromStart) {
					ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
					if(ssCdPtr->dtdate != tmpDate) // took a day off
						break;
					if(daysFromStart == 1) {
						// get training location while we are here
						strcpy(txbuf, returnUpper(ssCdPtr->zpostdesc));
						if(strstr(txbuf,"TRAVEL")) {
							if(ssCdPtr->znote) {
								strcpy(txbuf, returnUpper(ssCdPtr->znote));
								wc = split(txbuf, " ", wptrs);
								if(wc >= 3 && strcmp(wptrs[0],"TRAVEL") == 0 && (strcmp(wptrs[1],"TO") == 0 || strcmp(wptrs[1],"FROM") == 0)) {
									//allPtr = getAirportLatLonInfoByIcao(wptrs[2]);
									allPtr = getAirportInfoByIcao(wptrs[2]);
									if(allPtr)
										preCrewPtr->startLoc = allPtr->airportID;
								}
							}
						}
					}
					if(strlen(ssCdPtr->zacccodeid) == 0) // this was not an overtime day
						break;

					// doesn't matter if they are willing to work before planning window.
					//if((tmpDate & DateOnly) < (pwStartDate & DateOnly))
					//	break;
	
					strcpy(lbuf, returnUpper(ssCdPtr->zpostdesc)); // stop if they are doing training or office work
					if(!strstr(lbuf,"REC NOT IN")) {
						if(cantFly(lbuf))
							break;
					}
					preCrewPtr->startEarly = preCrewPtr->startEarly + 1.0;
					tmpDate = dt_addToDateTime(Hours, -24, tmpDate);
					//Find the temp start Loc from ssoft: RLZ 09/15/2008
					//We do not need the following bc we do not expect the code will be on OT days.
					/*
					if(ssCdPtr->znote) {
						if(strlen(ssCdPtr->znote)> 10){ //exclude short notes
							strcpy(txbuf, returnUpper(ssCdPtr->znote));
							wc = split(txbuf, " ", wptrs);
							if(wc >= 4 && strcmp(wptrs[0],"BEGIN") == 0 && (strcmp(wptrs[1],"TOUR") == 0 && strcmp(wptrs[2],"FROM") == 0)) {
								//allPtr = getAirportLatLonInfoByIcao(wptrs[2]);
								allPtr = getAirportInfoByIcao(wptrs[3]);
								if(allPtr)
									preCrewPtr->startLoc = allPtr->airportID;
							}
						}
					}
					*/
				}
			}
		}

		//# of days past tour pilot is willing to work after tour ends
		for(tmp = Minimum(preCrewPtr->ssCrewDataRoot); tmp; tmp = Successor(tmp)) {
			ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
			//if(ssCdPtr->dtdate == (preCrewPtr->tourEndTm & DateOnly))
			if(ssCdPtr->dtdate == (dt_addToDateTime(Hours, -24, preCrewPtr->tourEndTm))){
			// -24 because of we add 24 in tourEndTm.
				//Find the temp END Loc from ssoft: RLZ 09/15/2008
				if(ssCdPtr->znote) {
					if(strlen(ssCdPtr->znote)> 10){ //exclude short notes
						strcpy(txbuf, returnUpper(ssCdPtr->znote));
						wc = split(txbuf, " ", wptrs);
						if(wc >= 4 && strcmp(wptrs[0],"END") == 0 && (strcmp(wptrs[1],"TOUR") == 0 && strcmp(wptrs[2],"AT") == 0)) {
							//allPtr = getAirportLatLonInfoByIcao(wptrs[2]);
							allPtr = getAirportInfoByIcao(wptrs[3]);
							if(allPtr)
								preCrewPtr->endLoc = allPtr->airportID;
						}
					}	
				}			
				break;
			}
		}
		if(tmp) {
			int daysFromStart = 1;
			//AirportLatLon *allPtr;
			//char txbuf[128];
		
			if(strlen(ssCdPtr->zacccodeid) == 0) {
				// go forward counting OT days
				// while previous record date is current date +1
				tmpDate = dt_addToDateTime(Hours, 24, ssCdPtr->dtdate);
				for(tmp = Successor(tmp); tmp; tmp = Successor(tmp), ++daysFromStart) {
					ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
					if(ssCdPtr->dtdate != tmpDate) // took a day off
						break;
					//if(daysFromStart == 1) {
						// get training location while we are here
						//strcpy(txbuf, returnUpper(ssCdPtr->zpostdesc));
						//if(strstr(txbuf,"TRAVEL")) {
						//	strcpy(txbuf, returnUpper(ssCdPtr->znote));
						//	wc = split(txbuf, " ", wptrs);
						//	if(wc >= 3 && strcmp(wptrs[0],"TRAVEL") == 0 && (strcmp(wptrs[1],"TO") == 0 || strcmp(wptrs[1],"FROM") == 0)) {
						//		allPtr = getAirportLatLonInfoByIcao(wptrs[2]);
						//		if(allPtr)
						//			preCrewPtr->endLoc = allPtr->airportID;
						//	}
						//}
					//}
					if(strlen(ssCdPtr->zacccodeid) == 0) // this was not an overtime day
						break;

					// doesn't matter if they are willing to work after planning window.
					//if((tmpDate & DateOnly) > (pwEndDate & DateOnly))
					//	break;
	
					strcpy(lbuf, returnUpper(ssCdPtr->zpostdesc)); // stop if they are doing training or office work
					if(!strstr(lbuf,"REC NOT IN")) {
						if(cantFly(lbuf))
							break;
					}
					preCrewPtr->stayLate = preCrewPtr->stayLate + 1.0;
					tmpDate = dt_addToDateTime(Hours, 24, tmpDate);

					//Find the temp END Loc from ssoft: RLZ 09/15/2008
					//We do not need the following bc we do not expect the code will be on OT days.
					/*
					if(ssCdPtr->znote) {
						if(strlen(ssCdPtr->znote)> 10){ //exclude short notes
							strcpy(txbuf, returnUpper(ssCdPtr->znote));
							wc = split(txbuf, " ", wptrs);
							if(wc >= 4 && strcmp(wptrs[0],"END") == 0 && (strcmp(wptrs[1],"TOUR") == 0 && strcmp(wptrs[2],"AT") == 0)) {
								//allPtr = getAirportLatLonInfoByIcao(wptrs[2]);
								allPtr = getAirportInfoByIcao(wptrs[3]);
								if(allPtr)
									preCrewPtr->endLoc = allPtr->airportID;
							}
						}
					}	
					*/
				}
			}
		}
//###################################################################################################################
//		tmpDays = (int) preCrewPtr->stayLate;
//		tmpTourEndTm = AddDays(tmpDays,preCrewPtr->tourEndTm);
//		if(tmpTourEndTm < pwStartDate)
//			preCrewPtr->availDuringPlanningWindow = 0;
//		else if((tmpDate = getScheduledOnOff(preCrewPtr, tmpTourEndTm, 0)) != BadDateTime) {
//			if(tmpDate < pwStartDate)
//				preCrewPtr->availDuringPlanningWindow = 0;
//		}
//		if(preCrewPtr->availDuringPlanningWindow) {
//			getAcFlownList(preCrewPtr);
//			getDutySoFar00(preCrewPtr);
//		}
//###################################################################################################################
//              was in getTourStartEnd
//		(void) getScheduledOnOff(preCrewPtr, ssCdPtr->dtdate, 2, &cs_cdPtr);
//		if(cs_cdPtr) {
//			if(cs_cdPtr->actual_off) {
//				//planning window must start today. this crew member is already off work.
//				preCrewPtr->availDuringPlanningWindow = 0;
//				return;
//			}
//			tmpDate = ((cs_cdPtr->actual_on) ? cs_cdPtr->actual_on : cs_cdPtr->scheduled_on);
//			preCrewPtr->tourEndTm = dt_addToDateTime(Minutes,optParam.maxDutyTm, tmpDate);
//		}
//		else
//###################################################################################################################
		tmpDays = (int) preCrewPtr->stayLate;
		tmpTourEndTm = AddDays(tmpDays,preCrewPtr->tourEndTm);
		if(tmpTourEndTm <= pwStartDate)
					preCrewPtr->availDuringPlanningWindow = 0;
		/*
		if(tmpDays) {
			tmpTourEndTm = AddDays(tmpDays,preCrewPtr->tourEndTm);
			(void) getScheduledOnOff(preCrewPtr, tmpTourEndTm, 2, &cs_cdPtr);
			if(cs_cdPtr) {
				if(OnSSday(pwStartDate,tmpTourEndTm)) {
					if(cs_cdPtr->actual_off) //Look at this, RLZ
						preCrewPtr->availDuringPlanningWindow = 0; //their tour is already over
					else {
						tmpDate = ((cs_cdPtr->actual_on) ? cs_cdPtr->actual_on : cs_cdPtr->scheduled_on);
						tmpTourEndTm = dt_addToDateTime(Minutes,optParam.maxDutyTm, tmpDate);
						if(tmpTourEndTm <= pwStartDate)
							preCrewPtr->availDuringPlanningWindow = 0; //tour will be over before planning window start
					}
				}
			}
			else {
				if(tmpTourEndTm < pwStartDate)
					preCrewPtr->availDuringPlanningWindow = 0;
			}
		}
		(void) getScheduledOnOff(preCrewPtr, preCrewPtr->tourEndTm, 2, &cs_cdPtr);
		if(cs_cdPtr) {
			if(cs_cdPtr->actual_off) //Look at this, RLZ
				//planning window must start today. this crew member is already off work.
				preCrewPtr->availDuringPlanningWindow = 0;
			else {
				tmpDate = ((cs_cdPtr->actual_on) ? cs_cdPtr->actual_on : cs_cdPtr->scheduled_on);
				preCrewPtr->tourEndTm = dt_addToDateTime(Minutes,optParam.maxDutyTm, tmpDate);
				//The getHometime is soly from ssoftcrew per Susan. --Roger 3/12/2007 some minor work is needed.
				
			}
		}
		*/
       //populate the last travel leg for crew
		if(optParam.withCTC) {
			preCrewPtr->csTravelRoot =NULL;
			if((cstrlBuf = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) 
			{   logMsg(logFile,"%s Line %d, Out of Memory in readCrewLista().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			}
			memset(cstrlBuf, '\0', sizeof(CsTravelData));
			cstrlBuf->crewID = preCrewPtr->crewID;
			lkRet = Lookup(travel_flightRoot, cstrlBuf, travelFlightCmp, &tmp);
			switch(lkRet) {
			case EmptyTree:
			case GreaterThanLastItem:
			case ExactMatchFound: // impossible, since we just entered crewid
				//It is possible, no travel data.
				logMsg(logFile,"%s Line %d: crewid %d not found in travel_flight data. lkRet=%d\n", __FILE__,__LINE__, preCrewPtr->crewID, lkRet);
				break;
			case NotFoundReturnedNextItem:
				for(; tmp; tmp = Successor(tmp)) {
					cstrlPtr = (CsTravelData *) getTreeObject(tmp);
					if(cstrlPtr->crewID != preCrewPtr->crewID)
						break;
				//	if((AddDays(-(int) preCrewPtr->startEarly,preCrewPtr->tourStartTm)<= cstrlPtr->travel_dptTm && cstrlPtr->travel_dptTm <= AddDays((int) preCrewPtr->stayLate,preCrewPtr->tourEndTm))|| 
				//		(AddDays(-(int) preCrewPtr->startEarly,preCrewPtr->tourStartTm)<= cstrlPtr->travel_arrTm && cstrlPtr->travel_arrTm <= AddDays((int) preCrewPtr->stayLate,preCrewPtr->tourEndTm)))
					//09/04: replaced by the following simpler condition, RLZ.
					if (AddDays(-(int) preCrewPtr->startEarly,preCrewPtr->tourStartTm)<= cstrlPtr->travel_arrTm && cstrlPtr->travel_dptTm <= AddDays((int) preCrewPtr->stayLate,preCrewPtr->tourEndTm))
						if(!(preCrewPtr->csTravelRoot = RBTreeInsert(preCrewPtr->csTravelRoot, cstrlPtr, travelFlightCmp))) {
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn); 
						continue;
						//exit(1);
						}
				}
				break;
			}

			if(preCrewPtr->csTravelRoot == NULL)
				preCrewPtr->lastCsTravelLeg = NULL;
			else {
				tmp = Maximum(preCrewPtr->csTravelRoot);
				preCrewPtr->lastCsTravelLeg = (CsTravelData *) getTreeObject(tmp);
			}
		}
		
		if(preCrewPtr->availDuringPlanningWindow) {

			//getAcFlownList(preCrewPtr);
			getAdjCrewAssg(preCrewPtr); // Roger: 10/03, on1, on2.
			getLastActivityLeg(preCrewPtr);
			if(optParam.withCTC){
			  getDutySoFarWithCTC01(preCrewPtr);
			  //START - If the crew has at least 5 hrs of working time between availDT and travel departure time, utilize him - 12/04/08 ANG
			  if(preCrewPtr->availDuringPlanningWindow == 0 && preCrewPtr->cscd_availDT > 0 ){ //&& preCrewPtr->canStartLater == 0){
				 if( !(preCrewPtr->lastActivityLeg) //lastActivityLeg is not populated
					 && preCrewPtr->lastCsTravelLeg //travel is populated
					 && DateTimeToTime_t(preCrewPtr->tourEndTm) > optParam.windowStart + 5*60*60 //without looking at availability, crew has at least 5 hours in the planning window
					 && dt_addToDateTime(Minutes, optParam.postArrivalTime, preCrewPtr->lastCsTravelLeg->travel_arrTm) < dt_addToDateTime(Hours, -24+24*preCrewPtr->stayLate, preCrewPtr->tourEndTm)){ //travel is within stay late window - 24 hours
						preCrewPtr->availDuringPlanningWindow = 1;
						getDutySoFar01(preCrewPtr);
				 }
			  }
			  //END - 12/04/08 ANG
			}
			else
			  getDutySoFar01(preCrewPtr);
			if(optParam.earlierAvailability == 1){ // 10/29/07 ANG

				getDutySoFarAlt(preCrewPtr); 

				//radLastLegPtr = preCrewPtr->lastActivityLeg;
                //if (!preCrewPtr->lastCsTravelLeg)
                //    getDutySoFarAlt(preCrewPtr); 
				//else if (radLastLegPtr && 
				//	(strcmp(radLastLegPtr->rowtype,"logmgdleg") == 0 || strcmp(radLastLegPtr->rowtype,"mgdleg") == 0) &&
				//	(radLastLegPtr->rec_intime >= dt_addToDateTime(Minutes, optParam.postArrivalTime, preCrewPtr->lastCsTravelLeg->travel_arrTm)))
				//	getDutySoFarAlt(preCrewPtr); 	

			}

           
			
			if(preCrewPtr->availDuringPlanningWindow != 0) 
			{
				if(preCrewPtr->cscd_availDT >= (dt_addToDateTime(Minutes, (int)(24.0 * 60.0 * preCrewPtr->stayLate), preCrewPtr->tourEndTm)))
					preCrewPtr->availDuringPlanningWindow = 0;
			}
//			if(preCrewPtr->crewID == 44051) {                                              //debug
//				RawAircraftData *radPtr;                                               //debug
//				fprintf(logFile,"\nDebug Data for flownRoot for crewid 44051\n");      //debug
//				showRawAircraftDataHeader();                                           //debug
//				for(tmp = Minimum(preCrewPtr->flownRoot); tmp; tmp = Successor(tmp)) { //debug
//					radPtr = (RawAircraftData *) getTreeObject(tmp);               //debug
//					showRawAircraftData(radPtr);                                   //debug
//				}                                                                      //debug
//				showRawAircraftDataFooter();                                           //debug
//			}                                                                              //debug
		}
		//TEST  -- RLZ: 02/03/09 No need to have this, and the logic seems plausible.
		//tmpDate = dt_addToDateTime(Hours, -preCrewPtr->startEarly*24, preCrewPtr->tourStartTm);
		//if(tmpDate >= pwStartDate && tmpDate <= pwEndDate)// && preCrewPtr->lastActivityLeg == NULL)
		// fprintf(logFile, "traveling crews %s, %d\n",preCrewPtr->employeenumber, preCrewPtr->crewID);
		//TEST
		++preCrewPtr;
	}
	
	// display preCrew.
	if(verbose && verbose1) {
		fprintf(logFile,"A '+' in dtdate column to the left of the date means this is when their tour started.\n");
		fprintf(logFile,"This date can be before the start of the planning window.\n\n");
		fprintf(logFile,"A '-' in dtdate column to the right of the date means this is when their tour ends\n");
		fprintf(logFile,"as far as the planning window is concerned.\n\n");
		//showCombined_CrewDataPreCrewHeader("Combined data with tour start and end");
		preCrewPtr = preCrewList;
		while(preCrewPtr->employeenumber) {
			if(preCrewPtr->availDuringPlanningWindow == 0) {
				++preCrewPtr;
				continue;
			}
			//debug
			//if(preCrewPtr->newLogic == 0) {
			//	++preCrewPtr;
			//	continue;
			//}
			//end debug
			showCombined_CrewDataPreCrewHeader("Combined data with tour start and end");
			showCombined_CrewDataPreCrew(preCrewPtr);
			//fprintf(logFile,"  +----------+----------------+-----------+------------------------+--------+------------+--------+-----+------------------+------------------+");
			//fprintf(logFile,"------+------------+----------+\n");
			++preCrewPtr;
		}
		//showCombined_CrewDataPreCrewFooter((char *) 0);
	}


	// display preCrew. on-off, ... Roger
	if(verbose) {
		fprintf(logFile,"Pre Crew with ON1, OFF1, ON2, OFF2 \n");
		fprintf(logFile,"+------+-------+------------------+------------------+------------------+------------------+-------+------------------+----+-------+-------+------+------------------+------------------+\n");
		fprintf(logFile,"|empnum|crewid |    On1           |   Off1           | On2              | Off2             |avaiAPT| cs_avaiDT        | CL | DutyT |BlockT |STRAT | tourStart        | tourEnd          |\n");
		fprintf(logFile,"+------+-------+------------------+------------------+------------------+------------------+-------+------------------+----+-------+-------+------+------------------+------------------+\n");
		preCrewPtr = preCrewList;
		while(preCrewPtr->employeenumber) {
			if(preCrewPtr->availDuringPlanningWindow == 0) {
				++preCrewPtr;
				continue;
			}
			showCombined_CrewDataPreCrew_OnOff(preCrewPtr);
			++preCrewPtr;
			fprintf(logFile,"+-------+------------------+------------------+------------------+------------------+-------+------------------+----+-------+-------+------+------------------+------------------+\n");
		}
		   
	}

	//exit (0);
	crewCount = 0;
	preCrewPtr = preCrewList;
	while(preCrewPtr->employeenumber) {
		if(preCrewPtr->availDuringPlanningWindow != 0)
			crewCount++;
		++preCrewPtr;
	}

	// fill in crewList
	if(!(crewList = (Crew *) calloc((size_t) crewCount + 1, sizeof(Crew)))) {
		logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	numCrew = crewCount;

	cPtr = crewList;
	preCrewPtr = preCrewList;
	while(preCrewPtr->employeenumber) {
		if(preCrewPtr->availDuringPlanningWindow == 0) {
			++preCrewPtr;
			continue;
		}
		getCrewCategoryID(preCrewPtr);
		cPtr->categoryID = preCrewPtr->categoryID;
		cPtr->crewID = preCrewPtr->crewID;
		cPtr->position = preCrewPtr->position;
		cPtr->aircraftTypeID = preCrewPtr->aircraftTypeID;
		cPtr->tourStartTm = DateTimeToTime_t(preCrewPtr->tourStartTm);
		cPtr->tourEndTm = DateTimeToTime_t(preCrewPtr->tourEndTm);
		cPtr->startEarly = preCrewPtr->startEarly;
		cPtr->stayLate = preCrewPtr->stayLate;

		cPtr->startLoc = preCrewPtr->startLoc; 
		cPtr->endLoc = preCrewPtr->endLoc;
		cPtr->availAirportID = preCrewPtr->availAirportID;	
		cPtr->availDT = DateTimeToTime_t(preCrewPtr->cscd_availDT);

		cPtr->activityCode = preCrewPtr->cscd_canStartLater;
		cPtr->dutyTime = preCrewPtr->dutyTime;
		cPtr->blockTm = preCrewPtr->blockTime;
		cPtr->inTourTransfer = preCrewPtr->inTourTransfer;

		//Jintao's change for travel request
		if(preCrewPtr->lastActivityLeg){
		   cPtr->lastActivityLeg_flag = 1;
		   cPtr->lastActivityLeg_arrAptID = preCrewPtr->lastActivityLeg->inaptid;
		   cPtr->lastActivityLeg_aircraftID = preCrewPtr->lastActivityLeg->aircraftid;
		   cPtr->lastActivityLeg_recout = DateTimeToTime_t(preCrewPtr->lastActivityLeg->rec_outtime);
		}
		else{
		   cPtr->lastActivityLeg_flag = 0;
           cPtr->lastActivityLeg_recout = 0;
		}


		//Timezone is already adjusted when dates are in, do not need this any more.
		//timeZoneByAirport = timeZoneAdjByApt(preCrewPtr->availAirportID, preCrewPtr->cscd_availDT); 
		//if (preCrewPtr->cscd_availDT == preCrewPtr->tourStartTm || cPtr->availDT == (cPtr->tourStartTm - cPtr->startEarly *24 * 3600))
		//	cPtr->availDT  += timeZoneByAirport; //Roger

		if(cPtr->blockTm == 0 && cPtr->activityCode == 1 && preCrewPtr->tourStartTm > pwStartDate) {
			//AirportLatLon *allPtr;
			cPtr->activityCode = 2;
			//allPtr = getAirportLatLonInfoByAptID(cPtr->availAirportID);
			/* adjust GMT time for timezone */  //Roger  Convert it to a function
			//cPtr->tourStartTm += timeZoneByAirport;
	
		}

	   //Timezone adjust for temp base (start or end) RLZ 09/16/2008

		if (preCrewPtr->startLoc != preCrewPtr->baseairportid){
			timeZoneDiff = timeZoneAdjByApt(preCrewPtr->startLoc, preCrewPtr->tourStartTm) - timeZoneAdjByApt(preCrewPtr->baseairportid, preCrewPtr->tourStartTm);
			cPtr->tourStartTm = DateTimeToTime_t( dt_addToDateTime(Minutes, timeZoneDiff/60, preCrewPtr->tourStartTm));
		}

		if (preCrewPtr->endLoc != preCrewPtr->baseairportid){
			timeZoneDiff = timeZoneAdjByApt(preCrewPtr->endLoc, preCrewPtr->tourEndTm) - timeZoneAdjByApt(preCrewPtr->baseairportid, preCrewPtr->tourEndTm);
			cPtr->tourEndTm = DateTimeToTime_t( dt_addToDateTime(Minutes, timeZoneDiff/60, preCrewPtr->tourEndTm));
		}

       

		// add to crewList binary tree.
		if(!(crewListRoot = RBTreeInsert(crewListRoot, cPtr, crewListCrewidCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
			writeWarningData(myconn); 
			exit(1); //This should never happen. 
		}

		++cPtr;
		++preCrewPtr;
	}

	// display final crewList
	showCrewHeader();
	for(tmp = Minimum(crewListRoot); tmp; tmp = Successor(tmp)) {
		cPtr = (Crew *) getTreeObject(tmp);
		showCrew(cPtr);
	}
	showCrewFooter();

	//set up pre crew pair list
	preCrewPtr = preCrewList;
	 //RLZ Thanksgiving
	while(preCrewPtr->employeenumber) {
		if(preCrewPtr->availDuringPlanningWindow == 0) {
			++preCrewPtr;
			continue;
		}
		memset(&cdBuf,'\0',sizeof(cdBuf));
		cdBuf.crewid = preCrewPtr->crewID;
		lkRet = Lookup(crewidAcidStarttimeRoot, &cdBuf, crewidAcidStarttimeCmp, &tmp);
		switch(lkRet) {
		case EmptyTree:
		case GreaterThanLastItem:
		case ExactMatchFound: // impossible, since we don't use the whole key
			logMsg(logFile,"%s Line %d: crewid %s not found. lkRet=%s\n", __FILE__,__LINE__, preCrewPtr->employeenumber, bintreeRet[lkRet]); 
			break;
		case NotFoundReturnedNextItem:
			for(;tmp; tmp=Successor(tmp)) {
				cdPtr = (CrewData *) getTreeObject(tmp);
				if(cdPtr->crewid != preCrewPtr->crewID)
					break;
				if(!(cdPtr->starttime <= pwEndDate && cdPtr->endtime >= AddDays(-DAYS_BEOFRE_PWS, pwStartDate))) //RLZ, 10/11/2007
					continue;
				memset(&preCrewPairBuf, '\0', sizeof(preCrewPairBuf));
				preCrewPairBuf.aircraftid = cdPtr->aircraftid;
				tmp1 = TreeSearch(preCrewPairRoot, &preCrewPairBuf, preCrewPairCmp);
				if(!tmp1) {
					// start a new pre crew pair
					if(!(preCrewPairPtr = (PRE_Crew_Pair *) calloc((size_t) 1, sizeof(PRE_Crew_Pair)))) {
						logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn); exit(1);
					}
					preCrewPairPtr->aircraftid = cdPtr->aircraftid;
					if(!(preCrewPairPtr->starttimeCrewidRoot =
						RBTreeInsert(preCrewPairPtr->starttimeCrewidRoot, cdPtr, starttimeCrewidCmp))) {
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn); 
						exit(1);
					}
					if(!(preCrewPairRoot = RBTreeInsert(preCrewPairRoot, preCrewPairPtr, preCrewPairCmp))) {
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList(). acid=%d, reg=%s, crewid=%d\n",
							__FILE__,__LINE__,
							cdPtr->aircraftid, cdPtr->registration, cdPtr->crewid);
						writeWarningData(myconn); 
						exit(1);
					}
				}				
				else {
					preCrewPairPtr = (PRE_Crew_Pair *) getTreeObject(tmp1);
					if(!(preCrewPairPtr->starttimeCrewidRoot =
						RBTreeInsert(preCrewPairPtr->starttimeCrewidRoot, cdPtr, starttimeCrewidCmp))) {
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn); 
						exit(1);
					}
				}				
			}

			break;
		}
		++preCrewPtr;
	}

	

	//display pre crew pair list
	if(verbose) {
		fprintf(logFile,"\n Pre Crew Pairs\n");
		fprintf(logFile,"+--------+--------+-----+-------+--------+------------------+------------------+\n");
		fprintf(logFile,"| crewid | empnum | pos | acid  | reg    | starttime        | endtime          |\n");
		fprintf(logFile,"+--------+--------+-----+-------+--------+------------------+------------------+\n");
		for(tmp = Minimum(preCrewPairRoot);tmp; tmp=Successor(tmp)) {
			preCrewPairPtr = (PRE_Crew_Pair *) getTreeObject(tmp);
			for(tmp1 = Minimum(preCrewPairPtr->starttimeCrewidRoot); tmp1; tmp1 = Successor(tmp1)) {
				cdPtr = (CrewData *) getTreeObject(tmp1);
				fprintf(logFile,"| %6d | %6s | %3d | %5d | %6s | %s | %s |\n",
					cdPtr->crewid,
					cdPtr->employeenumber,
					cdPtr->position,
					cdPtr->aircraftid,
					cdPtr->registration,
	    				dt_DateTimeToDateTimeString(cdPtr->starttime, tbuf1, "%Y/%m/%d %H:%M"),
	    				dt_DateTimeToDateTimeString(cdPtr->endtime, tbuf2, "%Y/%m/%d %H:%M"));
			}
		}
		fprintf(logFile,"+--------+--------+-----+-------+--------+------------------+------------------+\n");
	}

//###########################################################################################################################################
// for each PRE_Crew
// do
// 	for each pcpr.aircraftid in preCrewPairRoot pcpr
// 	do
// 		for each cd1 in pcpr.starttimeCrewidRoot 
// 		do
// 			for each cd2 in pcpr.starttimeCrewidRoot
// 			do
// 				if cd1.starttime / cd1.endtime overlaps cd2.starttime / cd2.endtime and cd1.crewid != cd2.crewid
// 					if pair does not already exist
// 						generate a new pre crew pair
//			done
//	done
//done
//###########################################################################################################################################
	for(tmp = Minimum(preCrewPairRoot);tmp; tmp=Successor(tmp)) {
		preCrewPairPtr = (PRE_Crew_Pair *) getTreeObject(tmp);
		for(tmp1 = Minimum(preCrewPairPtr->starttimeCrewidRoot); tmp1; tmp1 = Successor(tmp1)) {
			cdPtr1 = (CrewData *) getTreeObject(tmp1);
			for(tmp2 = Minimum(preCrewPairPtr->starttimeCrewidRoot); tmp2; tmp2 = Successor(tmp2)) {
				cdPtr2 = (CrewData *) getTreeObject(tmp2);
				if(cdPtr1->crewid == cdPtr2->crewid)
					continue;
// fix: see below for another way to fix pair with the same crewid
				if(cdPtr1->position > 1 && cdPtr2->position > 1) // Roger: ignoring the illegal pairing 
                    continue;

				if(cdPtr1->position == 1 && cdPtr2->position == 1) // Roger: including PIC, PIC, b/c current data should not allow PIC/PIC.
					continue;


// comment this and replacement "A" out if you use method below. We may need both. More work here..
// end fix

				// RLZ 07/11/08 REMOVE THE "="
				//if(!((cdPtr1->starttime <= cdPtr2->endtime && cdPtr1->endtime >= cdPtr2->starttime) ||
				 //   (cdPtr2->starttime <= cdPtr1->endtime && cdPtr2->endtime >= cdPtr1->starttime)))

				if(!((cdPtr1->starttime < cdPtr2->endtime && cdPtr1->endtime > cdPtr2->starttime) ||
				    (cdPtr2->starttime < cdPtr1->endtime && cdPtr2->endtime > cdPtr1->starttime)))
					continue;					
					
				//remove the fake/unpractical crew pairs.	
				dt_dateTimeDiff(Min(cdPtr1->endtime, cdPtr2->endtime), Max(cdPtr1->starttime, cdPtr2->starttime), &days, &hours, &minutes, &seconds, &msecs);
        if(((24 * 60 * days) + (60 * hours) + minutes) < MIN_CRPR_DUTYMINS)
          continue;	

				// we have different crewids and the start/end times overlap.
				// find out if we already have paired these two.
				if(cdPtr1->crewid < cdPtr2->crewid)
					pxBuf.pairkey = (((uint64) cdPtr1->crewid) << 32) | cdPtr2->crewid;
				else
					pxBuf.pairkey = (((uint64) cdPtr2->crewid) << 32) | cdPtr1->crewid;
				pxBuf.assignedAircraftID = preCrewPairPtr->aircraftid;
				pxBuf.pairStartTm = Max(cdPtr1->starttime, cdPtr2->starttime);
				pxBuf.pairEndTm = Min(cdPtr1->endtime, cdPtr2->endtime);
				if(TreeSearch(pxPairDupChkRoot, &pxBuf, pxPairDupChkCmp))
					continue;

				//this is a new pair. save just enough info to check for dups
				if(!(pxPtr = (CrewPairX *) calloc((size_t) 1, sizeof(CrewPairX)))) {
					logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
					writeWarningData(myconn); exit(1);
				}
				pxPtr->pairkey = pxBuf.pairkey;
				pxPtr->assignedAircraftID = pxBuf.assignedAircraftID;
				pxPtr->pairStartTm = pxBuf.pairStartTm;
				pxPtr->pairEndTm = pxBuf.pairEndTm;
				oldTree = pxPairDupChkRoot;
				if(!(pxPairDupChkRoot = RBTreeInsert(pxPairDupChkRoot, pxPtr, pxPairDupChkCmp))) {
					logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
					//warning
					pxPairDupChkRoot = oldTree;
					continue;
					//writeWarningData(myconn); exit(1);
				}

				// continue on as before
				//#######################################################################################################
				//if(!(pxPtr = (CrewPairX *) calloc((size_t) 1, sizeof(CrewPairX)))) {
				//	logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
				//	writeWarningData(myconn); exit(1);
				//}
				pxPtr->crewPairID = ++preNumCrewPair;
				pxPtr->crewMemA_pos = cdPtr1->position;
				pxPtr->crewMemA_crewid = cdPtr1->crewid;
				pxPtr->crewMemA_starttime = cdPtr1->starttime;
				pxPtr->crewMemA_endtime = cdPtr1->endtime;

				pxPtr->crewMemB_pos = cdPtr2->position;
				pxPtr->crewMemB_crewid = cdPtr2->crewid;
				pxPtr->crewMemB_starttime = cdPtr2->starttime;
				pxPtr->crewMemB_endtime = cdPtr2->endtime;

				pxPtr->assignedAircraftID = preCrewPairPtr->aircraftid;
				pxPtr->registration = cdPtr1->registration;
/* DON'T NEED TO GET truePairStartTm/endTm now. will do it in the final crew pair listRLZ 10/15/2007
				crewBuf.crewID = pxPtr->crewMemA_crewid;
				tmp3 = TreeSearch(crewListRoot, &crewBuf, crewListCrewidCmp);
				if(!tmp3) {
					logMsg(logFile,"%s Line %d, can't find crewid %d\n", __FILE__,__LINE__, crewBuf.crewID);
					writeWarningData(myconn); exit(1);
				}
				cPtr = (Crew *) getTreeObject(tmp3);

				crewBuf.crewID = pxPtr->crewMemB_crewid;
				tmp4 = TreeSearch(crewListRoot, &crewBuf, crewListCrewidCmp);
				if(!tmp4) {
					logMsg(logFile,"%s Line %d, can't find crewid %d\n", __FILE__,__LINE__, crewBuf.crewID);
					writeWarningData(myconn); exit(1);
				}
				c1Ptr = getTreeObject(tmp4);

				tmpDate1 = dt_addToDateTime(Minutes, - ((int)(24.0 * 60.0 * cPtr->startEarly)), dt_time_tToDateTime(cPtr->tourStartTm));
				tmpDate2 = dt_addToDateTime(Minutes, - ((int)(24.0 * 60.0 * c1Ptr->startEarly)), dt_time_tToDateTime(c1Ptr->tourStartTm));
				pxPtr->pairStartTm = Max(tmpDate1, tmpDate2);


				tmpDate1 = dt_addToDateTime(Minutes, ((int)(24.0 * 60.0 * cPtr->stayLate)), dt_time_tToDateTime(cPtr->tourEndTm));
				tmpDate2 = dt_addToDateTime(Minutes, ((int)(24.0 * 60.0 * c1Ptr->stayLate)), dt_time_tToDateTime(c1Ptr->tourEndTm));
				pxPtr->pairEndTm = Min(tmpDate1, tmpDate2);
*/

// replacement "A"
				/*
				if(pxPtr->crewMemA_pos != pxPtr->crewMemB_pos) {
					pxPtr->captainID = (pxPtr->crewMemA_pos == 1) ? pxPtr->crewMemA_crewid : pxPtr->crewMemB_crewid;
					pxPtr->flightOffID = (pxPtr->crewMemA_pos == 2) ? pxPtr->crewMemA_crewid : pxPtr->crewMemB_crewid;
					if(pxPtr->captainID == 0) //debug
						errNbr1 = 1; //debug
				}
				else {
					if(pxPtr->crewMemA_pos == 2) {
						logMsg(logFile,"%s Line %d, No PIC. crewpairid %d, crewid %d pos %d, crewid %d pos %d\n",
							__FILE__,__LINE__, pxPtr->crewPairID,
							pxPtr->crewMemA_crewid, pxPtr->crewMemA_pos,
							pxPtr->crewMemB_crewid, pxPtr->crewMemB_pos);
					}
					// arbitrarily set them up
					pxPtr->captainID = pxPtr->crewMemA_crewid;
					pxPtr->flightOffID = pxPtr->crewMemB_crewid;
				}
				*/
// end replacement "A"

				
// Potential way to fix the pair with the same crewid
// if you comment this back in comment out replacement "A" above
//THE FOLLOWING CAN BE simplified. RLZ: 10/15/2007
				if((pxPtr->crewMemA_pos == 1 || pxPtr->crewMemA_pos == 3) && (pxPtr->crewMemB_pos == 2 || pxPtr->crewMemB_pos == 4)) {
					pxPtr->captainID = pxPtr->crewMemA_crewid;
					pxPtr->flightOffID = pxPtr->crewMemB_crewid;
				}
				else if((pxPtr->crewMemA_pos == 2 || pxPtr->crewMemA_pos == 4) && (pxPtr->crewMemB_pos == 1 || pxPtr->crewMemB_pos == 3)) {
					pxPtr->captainID = pxPtr->crewMemB_crewid;
					pxPtr->flightOffID = pxPtr->crewMemA_crewid;
				}
				else if((pxPtr->crewMemA_pos == 1 || pxPtr->crewMemA_pos == 3) && (pxPtr->crewMemB_pos == 1 || pxPtr->crewMemB_pos == 3)) {
					// both PICS, arbitrarily pick one to be PIC and the other SIC 
					pxPtr->captainID = pxPtr->crewMemB_crewid;
					pxPtr->flightOffID = pxPtr->crewMemA_crewid;
				}
				else {
					logMsg(logFile,"%s Line %d, No PIC. crewpairid %d, crewid %d pos %d, crewid %d pos %d\n",
						__FILE__,__LINE__, pxPtr->crewPairID,
						pxPtr->crewMemA_crewid, pxPtr->crewMemA_pos,
						pxPtr->crewMemB_crewid, pxPtr->crewMemB_pos);
					// both SICS, arbitrarily pick one to be PIC and the other SIC 
					pxPtr->captainID = pxPtr->crewMemB_crewid;
					pxPtr->flightOffID = pxPtr->crewMemA_crewid;
				}
               
				pxPtr->hasFlown = hasAlreadyFlownThisPlane(cdPtr1->crewid, cdPtr1->aircraftid, pxPtr->pairStartTm);
				if(pxPtr->crewMemA_crewid < pxPtr->crewMemB_crewid)
					pxPtr->pairkey = (((uint64) pxPtr->crewMemA_crewid) << 32) | pxPtr->crewMemB_crewid;
				else
					pxPtr->pairkey = (((uint64) pxPtr->crewMemB_crewid) << 32) | pxPtr->crewMemA_crewid;

				//pxRoot is not used any more, only in display... Can be removed. RLZ 10/17/2007
				if(!(pxRoot = RBTreeInsert(pxRoot, pxPtr, pxCmp))) {
					logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
					writeWarningData(myconn); exit(1);
				}

				oldTree = pxPairRoot;
				if(!(pxPairRoot = RBTreeInsert(pxPairRoot, pxPtr, pxPairCmp))) {
					logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList(). aircraftid=%d crewMemA_crewid=%d crewMemB_crewid=%d\n",
						__FILE__,__LINE__, pxPtr->assignedAircraftID, pxPtr->crewMemA_crewid, pxPtr->crewMemB_crewid);
					writeWarningData(myconn); 
					pxPairRoot = oldTree;
					continue;
					//exit(1);
				}
				//debug
				//else
				//	logMsg(logFile,"%s Line %d, RBTreeInsert() succeeded: aircraftid=%d crewMemA_crewid=%d crewMemB_crewid=%d\n",
				//		__FILE__,__LINE__, pxPtr->assignedAircraftID, pxPtr->crewMemA_crewid, pxPtr->crewMemB_crewid);
				// end debug
				//#######################################################################################################

			}
		}
	}

	//combine entries where the pairkey is the same but with different aircraft
	stack = stkCreate();
	cpList = createQueue();
	for(tmp = Minimum(pxPairRoot); tmp; tmp = Successor(tmp)) {
		pxPtr = (CrewPairX *) getTreeObject(tmp);

		// allocate and initialize the array of aircraft
		if((!(pxPtr->aircraftIDPtr = calloc((size_t) 2, sizeof(int))))) {
			logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		pxPtr->aircraftCount = 1;
		*pxPtr->aircraftIDPtr = pxPtr->assignedAircraftID;

		// if stack is empty, push on stack
		if(stkGetCount(stack) == 0)
			stkPush(stack,pxPtr);

		else {
			// is this the same pair as the top of the stack			
			pxPtr0 = (CrewPairX *) stkPop(stack);
			if(pxPtr0->pairkey != pxPtr->pairkey) {
				// no it's not the same pair as the top of the stack.
				// make a CrewPair structure out of pxPtr0
				if(!(cpPtr = (CrewPair *) calloc((size_t) 1, sizeof(CrewPair)))) {
					logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
					writeWarningData(myconn); exit(1);
				}
				cpPtr->crewPairID = pxPtr0->crewPairID;
				cpPtr->captainID = pxPtr0->captainID;
				cpPtr->flightOffID = pxPtr0->flightOffID;
				cpPtr->aircraftID = pxPtr0->aircraftIDPtr;
				//cpPtr->pairkey = pxPtr0->pairkey; //New, RLZ, 10/15/2007
				if(!(cpPtr->lockTour = calloc((size_t) pxPtr0->aircraftCount + 1, sizeof(int)))) {
					logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
					writeWarningData(myconn); exit(1);
				}

				//cpPtr->pairStartTm = DateTimeToTime_t(pxPtr0->pairStartTm);
				//cpPtr->pairEndTm = DateTimeToTime_t(pxPtr0->pairEndTm); //Repopulate below RLZ.


				cpPtr->hasFlownFirst = pxPtr0->hasFlown; //This should be OK now RLZ, 10/15/2007, do not need the following
				//cpPtr->hasFlownFirst = pairHasAlreadyFlownThisPlane(pxPtr0, *(cpPtr->aircraftID));			
				
				//min(hasAlreadyFlownThisPlane(pxPtr0->crewMemA_crewid,*(cpPtr->aircraftID),pxPtr0->crewMemA_starttime),hasAlreadyFlownThisPlane(pxPtr0->crewMemB_crewid,*(cpPtr->aircraftID),pxPtr0->crewMemB_starttime));
				//Bug-hasFlownFirst, Temp fix. Roger

                //Get the true pairStartTm...
				getCrewPairStartEnd(cpPtr);//, pxPtr0);

				if (cpPtr->hasFlownFirst && cpPtr->pairStartTm >= optParam.windowStart)
					cpPtr->hasFlownFirst = 0;


				(void) QAddToTail(cpList, cpPtr);

				
                // I do not think, I need to have aircraftID as one of the compare keys, but I will keep it anyway, //RLZ, 10/15/2007
				for(x = 0; *((cpPtr->aircraftID) + x); ++x) {
					if(!(acpxPtr = (AircraftCrewPairXref *) calloc((size_t) 1, sizeof(AircraftCrewPairXref)))) {
						logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn); exit(1);
					}
					acpxPtr->aircraftid = *((cpPtr->aircraftID) + x);
					//acpxPtr->starttime = pxPtr0->crewMemA_starttime;
					//acpxPtr->endtime = pxPtr0->crewMemA_endtime;
					//acpxPtr->crewid = pxPtr0->crewMemA_crewid;
					acpxPtr->pairkey = pxPtr0->pairkey;
					acpxPtr->crewPairID = pxPtr0->crewPairID;
					//if(!TreeSearch(aircraftCrewPairXrefRoot, acpxPtr, aircraftCrewPairXrefCmp)) {
					oldTree = aircraftCrewPairXrefRoot;

					if(!(aircraftCrewPairXrefRoot = RBTreeInsert(aircraftCrewPairXrefRoot, acpxPtr, aircraftCrewPairXrefCmp))) {
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn); 
						aircraftCrewPairXrefRoot = oldTree;
						continue;		//ignore the duplicate record				
						//exit(1);
					}
					//} //RLZ does not think we need this check. 
						//It turned out that we do need this for some rare case, one pair with one aircraft (a), then b, then switch to a.

					
		            // Don't need for another crew..
					//if(!(acpxPtr = (AircraftCrewPairXref *) calloc((size_t) 1, sizeof(AircraftCrewPairXref)))) {
					//	logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
					//	writeWarningData(myconn); exit(1);
					//}
					//acpxPtr->aircraftid = *((cpPtr->aircraftID) + x);
					//acpxPtr->starttime = pxPtr0->crewMemB_starttime;
					//acpxPtr->endtime = pxPtr0->crewMemB_endtime;
					//acpxPtr->crewid = pxPtr0->crewMemB_crewid;
					//acpxPtr->crewPairID = pxPtr0->crewPairID;
					//if(!TreeSearch(aircraftCrewPairXrefRoot, acpxPtr, aircraftCrewPairXrefCmp)) {
					//	if(!(aircraftCrewPairXrefRoot = RBTreeInsert(aircraftCrewPairXrefRoot, acpxPtr, aircraftCrewPairXrefCmp))) {
					//		logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
					//		writeWarningData(myconn); exit(1);
					//	}
					//}
				}

				++numCrewPairs;

				// push pxPtr on the stack
				stkPush(stack,pxPtr);
			}
			else {
				//combine and push back on the stack
				if (pxPtr->assignedAircraftID != pxPtr0->assignedAircraftID){
					if ( pxPtr0->pairStartTm < pxPtr->pairStartTm && pxPtr->pairStartTm < pwStartDate){ 
						//find an assigned aircraft closer to PWS. first condition can be omitted. RLZ 10/16/07
						*(pxPtr0->aircraftIDPtr + (pxPtr0->aircraftCount - 1)) = pxPtr->assignedAircraftID;
						pxPtr0->hasFlown = pxPtr->hasFlown;
						//pxPtr0->assignedAircraftID = pxPtr->assignedAircraftID;						
					}
					else {
						pxPtr0->aircraftCount++;
						if(!(pxPtr0->aircraftIDPtr = realloc(pxPtr0->aircraftIDPtr,(size_t)(sizeof(int) * (pxPtr0->aircraftCount + 1))))) {
							logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
							writeWarningData(myconn); exit(1);
						}
						*(pxPtr0->aircraftIDPtr + (pxPtr0->aircraftCount - 1)) = pxPtr->assignedAircraftID;
						*(pxPtr0->aircraftIDPtr + pxPtr0->aircraftCount) = 0;
					}
				}
				
				//if(pxPtr->pairStartTm < pxPtr0->pairStartTm) {
				//	pxPtr0->pairStartTm = pxPtr->pairStartTm;
				//	pxPtr0->hasFlown = pxPtr->hasFlown;
				//}
				//if(pxPtr->pairEndTm > pxPtr0->pairEndTm)
				//	pxPtr0->pairEndTm = pxPtr->pairEndTm;
				pxPtr0->assignedAircraftID = pxPtr->assignedAircraftID;					
				if (pxPtr->hasFlown > pxPtr0->hasFlown) pxPtr0->hasFlown = pxPtr->hasFlown;
				stkPush(stack,pxPtr0);
			}
		}
	}
  
	while(stkGetCount(stack) > 0) {  //The last pair left in the stack, creat a new crew pair from it. Copy codes from above.
		pxPtr0 = (CrewPairX *) stkPop(stack);
		if(!(cpPtr = (CrewPair *) calloc((size_t) 1, sizeof(CrewPair)))) {
			logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		cpPtr->crewPairID = pxPtr0->crewPairID;
		cpPtr->captainID = pxPtr0->captainID;
		cpPtr->flightOffID = pxPtr0->flightOffID;
		cpPtr->aircraftID = pxPtr0->aircraftIDPtr;
		//cpPtr->pairkey = pxPtr0->pairkey; //RLZ
		if(!(cpPtr->lockTour = calloc((size_t) pxPtr0->aircraftCount + 1, sizeof(int)))) {
			logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		//cpPtr->pairStartTm = DateTimeToTime_t(pxPtr0->pairStartTm);
		//cpPtr->pairEndTm = DateTimeToTime_t(pxPtr0->pairEndTm);
		cpPtr->hasFlownFirst = pxPtr0->hasFlown; //
		//cpPtr->hasFlownFirst = pairHasAlreadyFlownThisPlane(pxPtr0, *(cpPtr->aircraftID));					
		//min(hasAlreadyFlownThisPlane(pxPtr0->crewMemA_crewid,*(cpPtr->aircraftID),pxPtr0->crewMemA_starttime),hasAlreadyFlownThisPlane(pxPtr0->crewMemB_crewid,*(cpPtr->aircraftID),pxPtr0->crewMemB_starttime));
		//Bug-hasFlownFirst, Temp fix. Roger
		
		getCrewPairStartEnd(cpPtr); //, pxPtr0);

		
		(void) QAddToTail(cpList, cpPtr);

                // I do not think, I need to have aircraftID as one of the compare keys, but I will keep it anyway, //RLZ, 10/15/2007
				for(x = 0; *((cpPtr->aircraftID) + x); ++x) {
					if(!(acpxPtr = (AircraftCrewPairXref *) calloc((size_t) 1, sizeof(AircraftCrewPairXref)))) {
						logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn); exit(1);
					}
					acpxPtr->aircraftid = *((cpPtr->aircraftID) + x);
					//acpxPtr->starttime = pxPtr0->crewMemA_starttime;
					//acpxPtr->endtime = pxPtr0->crewMemA_endtime;
					//acpxPtr->crewid = pxPtr0->crewMemA_crewid;
					acpxPtr->pairkey = pxPtr0->pairkey;
					acpxPtr->crewPairID = pxPtr0->crewPairID;
					oldTree = aircraftCrewPairXrefRoot;
					//if(!TreeSearch(aircraftCrewPairXrefRoot, acpxPtr, aircraftCrewPairXrefCmp)) {
					if(!(aircraftCrewPairXrefRoot = RBTreeInsert(aircraftCrewPairXrefRoot, acpxPtr, aircraftCrewPairXrefCmp))) {
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
						writeWarningData(myconn);
						aircraftCrewPairXrefRoot = oldTree;
						continue;
						//exit(1);
					}
					//} //RLZ does not think we need this check.

					
		            // Don't need for another crew..
					//if(!(acpxPtr = (AircraftCrewPairXref *) calloc((size_t) 1, sizeof(AircraftCrewPairXref)))) {
					//	logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
					//	writeWarningData(myconn); exit(1);
					//}
					//acpxPtr->aircraftid = *((cpPtr->aircraftID) + x);
					//acpxPtr->starttime = pxPtr0->crewMemB_starttime;
					//acpxPtr->endtime = pxPtr0->crewMemB_endtime;
					//acpxPtr->crewid = pxPtr0->crewMemB_crewid;
					//acpxPtr->crewPairID = pxPtr0->crewPairID;
					//if(!TreeSearch(aircraftCrewPairXrefRoot, acpxPtr, aircraftCrewPairXrefCmp)) {
					//	if(!(aircraftCrewPairXrefRoot = RBTreeInsert(aircraftCrewPairXrefRoot, acpxPtr, aircraftCrewPairXrefCmp))) {
					//		logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
					//		writeWarningData(myconn); exit(1);
					//	}
					//}
		}

		++numCrewPairs;
	}

	if(!(crewPairList = (CrewPair *) calloc((size_t) numCrewPairs + 1, sizeof(CrewPair)))) {
		logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	// set up sequential list of crew pairs //////////////////////////////////////////////////////////////////////////
	//
	//cpPtr1 = crewPairList;
	//while(QGetCount(cpList)) {
	//	cpPtr = (CrewPair *) QRmvFromHead(cpList);
	//	memcpy(cpPtr1, cpPtr, sizeof(CrewPair));
	//	++cpPtr1;
	//}
	while(QGetCount(cpList)) {
		cpPtr = (CrewPair *) QRmvFromHead(cpList);
		if(!(crewPairRoot = RBTreeInsert(crewPairRoot, cpPtr, crewPairCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
			writeWarningData(myconn); 
			exit(1);
		}

		if(!(crewPairACRoot = RBTreeInsert(crewPairACRoot, cpPtr, crewPairACCmp))) { // used for re-flag hasFlownFirst
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewList().\n",__FILE__,__LINE__);
			writeWarningData(myconn); 
			exit(1);
		}
	}


   //Correct any possible hasFlownFirst bug. No more than one crew pair can have "hasFlonwnFirst" on  the same aircraft.
	//We should rarely need it. Only the case 2 pairs have flown the same aircraft and both of them still have duty in the planning window. //RLZ 10/16/2007

	//cpPtr = crewPairList;

	//++cpPtr;
	//while(cpPtr->crewPairID) {
	//	cpPtr1 = cpPtr - 1;
	//	if (*(cpPtr->aircraftID) == *(cpPtr1->aircraftID) && cpPtr->hasFlownFirst && cpPtr1->hasFlownFirst){
	//		//If both have flown the same ac
	//		fprintf(logFile,"Warning: hasFlownFirst bug: \n | %4d | %6d | %6d | %4d | \n",
	//			cpPtr->crewPairID,
	//			cpPtr->captainID,
	//			cpPtr->flightOffID,
	//			*(cpPtr->aircraftID));
	//		if (cpPtr->pairEndTm < cpPtr1->pairEndTm){ // set hasFlonwFirst =  1 for the pair with longer pairEnd and 0 for the other
	//			cpPtr->hasFlownFirst = 0;
	//		}
	//		else
	//			cpPtr1->hasFlownFirst = 0;

	//	}
	//	++cpPtr;
	//}

	tmp = Minimum(crewPairACRoot);
	cpPtr = (CrewPair *) getTreeObject(tmp);
    
	for(tmp = Successor(tmp); tmp; tmp = Successor(tmp)) {
		cpPtr1 = (CrewPair *) getTreeObject(tmp);
		if (*(cpPtr->aircraftID) == *(cpPtr1->aircraftID) && cpPtr->hasFlownFirst && cpPtr1->hasFlownFirst){
			//If both have flown the same ac
			fprintf(logFile,"Warning: hasFlownFirst issue: \n | %4d | %6d | %6d | %4d | \n",
				cpPtr->crewPairID,
				cpPtr->captainID,
				cpPtr->flightOffID,
				*(cpPtr->aircraftID));
			if (cpPtr->pairEndTm <= cpPtr1->pairEndTm){ // set hasFlonwFirst =  1 for the pair with longer pairEnd and 0 for the other
				cpPtr->hasFlownFirst = 0;
			}
			else
				cpPtr1->hasFlownFirst = 0;
		}
		cpPtr = cpPtr1;
	}

//Populate the final crew pair list
	cpPtr1 = crewPairList;
	for(tmp = Minimum(crewPairRoot); tmp; tmp = Successor(tmp)) {
		cpPtr = (CrewPair *) getTreeObject(tmp);
		memcpy(cpPtr1, cpPtr, sizeof(CrewPair));
		++cpPtr1;
	}

	//START - 11/01/07 ANG
	//insert codes here to capture crews to which fake demands are to be created to bring them home
	if(optParam.autoFlyHome == 1){
		crewEndTourList = (CrewEndTourRecord *) calloc((size_t) 2*optParam.maxFakeMaintRec, (size_t) sizeof(CrewEndTourRecord));// 11/12/07 ANG
		preCrewPtr = preCrewList;
		while(preCrewPtr->employeenumber) {
			if(preCrewPtr->availDuringPlanningWindow == 0) {
				++preCrewPtr;
				continue;
			}//end if

			if(preCrewPtr->availDuringPlanningWindow != 0) 
			if(preCrewPtr->availDuringPlanningWindow > 0) // 04/08/08 ANG 
			{
				if(preCrewPtr->tourEndTm && preCrewPtr->crewID){
					if((pwStartDate < preCrewPtr->tourEndTm) && (preCrewPtr->tourEndTm <= pwEndDate)){
						//If the tourEndTm is within the planning window, create fake maintenance record
						if (numFakeMaintenanceRecord < optParam.maxFakeMaintRec){ //to be updated later with parameter 
							//include codes to check if fake record intercept current scheduled maintenance records
							addFakeRecords(preCrewPtr);
						} else {
							logMsg(logFile, "Fake record for crewID = %d with tourEndTm = %s is NOT added.  Number exceeds limit.\n", preCrewPtr->crewID, dt_DateTimeToDateTimeString(preCrewPtr->tourEndTm, tbuf1, "%Y/%m/%d %H:%M"));
						}
					}
				}
			}
			++preCrewPtr;
		}//end while
		qsort(maintList, numMaintenanceRecord, sizeof(MaintenanceRecord), compareMaintLegSchedOut); //sort maintList after adding fake records - 04/08/08 ANG
		printFakeRecords(); //display maintList after adding fake records 
	}//end if 
	//END - 11/01/07 ANG

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	if(verbose) {
		int *acPtr;

		fprintf(logFile,"pxRoot-- Small Day Pairs \n ");
		fprintf(logFile,"+------+--------+-----+--------+------------------+------------------+-----+--------+--------+------------------+------------------+\n");
		fprintf(logFile,"| crew |        |     |        |                  |                  |     | cap    | flight | pair             | pair             |\n");
		fprintf(logFile,"| pair |        |     | crew   |                  |                  | has | tain   | off    | start            | end              |\n");
		fprintf(logFile,"| id   | reg    | pos | id     | starttime        | endtime          | fln | id     | id     | time             | time             |\n");
		for(tmp = Minimum(pxRoot); tmp; tmp = Successor(tmp)) {
			pxPtr = (CrewPairX *) getTreeObject(tmp);
			fprintf(logFile,"+------+--------+-----+--------+------------------+------------------+-----+--------+--------+------------------+------------------+\n");
			fprintf(logFile,"| %4d | %6s | %3d | %6d | %s | %s | %3d | %6d | %6d | %s | %s |\n",
				pxPtr->crewPairID,
				pxPtr->registration,
				pxPtr->crewMemA_pos,
				pxPtr->crewMemA_crewid,
	    			dt_DateTimeToDateTimeString(pxPtr->crewMemA_starttime, tbuf1, "%Y/%m/%d %H:%M"),
	    			dt_DateTimeToDateTimeString(pxPtr->crewMemA_endtime, tbuf2, "%Y/%m/%d %H:%M"),
				pxPtr->hasFlown,
				(pxPtr->captainID) ? pxPtr->captainID : 0,
				(pxPtr->flightOffID) ? pxPtr->flightOffID : 0,
	    			dt_DateTimeToDateTimeString(pxPtr->pairStartTm, tbuf1, "%Y/%m/%d %H:%M"),
	    			dt_DateTimeToDateTimeString(pxPtr->pairEndTm, tbuf2, "%Y/%m/%d %H:%M"));
			fprintf(logFile,"| %4d | %6s | %3d | %6d | %s | %s | %3s | %6s | %6s | %16s | %16s |\n",
				pxPtr->crewPairID,
				pxPtr->registration,
				pxPtr->crewMemB_pos,
				pxPtr->crewMemB_crewid,
	    			dt_DateTimeToDateTimeString(pxPtr->crewMemB_starttime, tbuf1, "%Y/%m/%d %H:%M"),
	    			dt_DateTimeToDateTimeString(pxPtr->crewMemB_endtime, tbuf2, "%Y/%m/%d %H:%M"), "", "", "", "", "");

		}
		fprintf(logFile,"+------+--------+-----+--------+------------------+------------------+-----+--------+--------+------------------+------------------+\n\n\n");
		fprintf(logFile,"\ncrew pairs ordered by smaller crew id of the pair.\n");
		fprintf(logFile,"+------+--------+-----+--------+------------------+------------------+-----+--------+--------+------------------+------------------+\n");
		fprintf(logFile,"| crew |        |     |        |                  |                  |     | cap    | flight | pair             | pair             |\n");
		fprintf(logFile,"| pair |        |     | crew   |                  |                  | has | tain   | off    | start            | end              |\n");
		fprintf(logFile,"| id   | reg    | pos | id     | starttime        | endtime          | fln | id     | id     | time             | time             |\n");
		for(tmp = Minimum(pxPairRoot); tmp; tmp = Successor(tmp)) {
			pxPtr = (CrewPairX *) getTreeObject(tmp);
			fprintf(logFile,"+------+--------+-----+--------+------------------+------------------+-----+--------+--------+------------------+------------------+\n");
			fprintf(logFile,"| %4d | %6s | %3d | %6d | %s | %s | %3d | %6d | %6d | %s | %s |\n",
				pxPtr->crewPairID,
				pxPtr->registration,
				pxPtr->crewMemA_pos,
				pxPtr->crewMemA_crewid,
	    			dt_DateTimeToDateTimeString(pxPtr->crewMemA_starttime, tbuf1, "%Y/%m/%d %H:%M"),
	    			dt_DateTimeToDateTimeString(pxPtr->crewMemA_endtime, tbuf2, "%Y/%m/%d %H:%M"),
				pxPtr->hasFlown,
				(pxPtr->captainID) ? pxPtr->captainID : 0,
				(pxPtr->flightOffID) ? pxPtr->flightOffID : 0,
	    			dt_DateTimeToDateTimeString(pxPtr->pairStartTm, tbuf1, "%Y/%m/%d %H:%M"),
	    			dt_DateTimeToDateTimeString(pxPtr->pairEndTm, tbuf2, "%Y/%m/%d %H:%M"));
			fprintf(logFile,"| %4d | %6s | %3d | %6d | %s | %s | %3s | %6s | %6s | %16s | %16s |\n",
				pxPtr->crewPairID,
				pxPtr->registration,
				pxPtr->crewMemB_pos,
				pxPtr->crewMemB_crewid,
	    			dt_DateTimeToDateTimeString(pxPtr->crewMemB_starttime, tbuf1, "%Y/%m/%d %H:%M"),
	    			dt_DateTimeToDateTimeString(pxPtr->crewMemB_endtime, tbuf2, "%Y/%m/%d %H:%M"), "", "", "", "", "");

		}
		fprintf(logFile,"+------+--------+-----+--------+------------------+------------------+-----+--------+--------+------------------+------------------+\n\n\n");

		//display final crew pair list
		/*  RLZ This infor will be printed in pairCrews. 03/11/2009
		cpPtr = crewPairList;
		fprintf(logFile,"\n\n\n\nFinal Crew Pair List:\n");
		fprintf(logFile,"+------+--------+--------+------------------+------------------+-----+\n");
		fprintf(logFile,"| crew | cap    | flight | pair             | pair             |     |\n");
		fprintf(logFile,"| pair | tain   | off    | start            | end              | has |\n");
		fprintf(logFile,"| id   | id     | id     | time             | time             | fln | aircraft id list\n");
		fprintf(logFile,"+------+--------+--------+------------------+------------------+-----+\n");
		while(cpPtr->crewPairID) {
			cpPtr->countAircraftID = 0;// 03/24/08 ANG
			fprintf(logFile,"| %4d | %6d | %6d | %s | %s | %3d |",
				cpPtr->crewPairID,
				cpPtr->captainID,
				cpPtr->flightOffID,
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(cpPtr->pairStartTm))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M"),
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(cpPtr->pairEndTm))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M"),
				cpPtr->hasFlownFirst);
			acPtr = cpPtr->aircraftID;
			while(*acPtr) {
				fprintf(logFile," %d", *acPtr);
				++acPtr;
			}
			fprintf(logFile,"\n");
			// debug -- temporary fix
//			if(cpPtr->crewPairID == 23 || cpPtr->crewPairID == 24 || cpPtr->crewPairID == 32 || cpPtr->crewPairID == 33 || cpPtr->crewPairID == 34 ||
//				cpPtr->crewPairID == 42 || cpPtr->crewPairID == 43)
//				cpPtr->hasFlownFirst = 0;
			// end debug
			++cpPtr;
		}
		fprintf(logFile,"+------+--------+--------+------------------+------------------+-----+\n");
		*/
	}

		//START - For Debug - 03/24/08 ANG
		/*cpPtr = crewPairList;
		fprintf(logFile,"\n\n\n\nCount AcID in Crew Pair List:\n");
		fprintf(logFile,"+------+--------+--------+------------------+------------------+------+\n");
		fprintf(logFile,"| crew | cap    | flight | pair             | pair             | tot  |\n");
		fprintf(logFile,"| pair | tain   | off    | start            | end              | a/c  |\n");
		fprintf(logFile,"| id   | id     | id     | time             | time             | IDs  |\n");
		fprintf(logFile,"+------+--------+--------+------------------+------------------+------+\n");
		while(cpPtr->crewPairID) {
			fprintf(logFile,"| %4d | %6d | %6d | %s | %s | %4d |",
				cpPtr->crewPairID,
				cpPtr->captainID,
				cpPtr->flightOffID,
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(cpPtr->pairStartTm))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M"),
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(cpPtr->pairEndTm))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M"),
				cpPtr->countAircraftID);
			fprintf(logFile,"\n");
			++cpPtr;
		}
		//END - For Debug - 03/24/08 ANG*/

	//if(verbose) {
	//	fprintf(logFile,"acpxPtr = (AircraftCrewPairXref *) getTreeObject(tmp1); \n ");
	//	fprintf(logFile,"+------+--------+-----+--------+------------------+------------------+-----+--------+--------+------------------+------------------+\n");
	//	fprintf(logFile,"| crew |        |     |        |                  |                  |     | cap    | flight | pair             | pair             |\n");
	//	fprintf(logFile,"| pair |        |     | crew   |                  |                  | has | tain   | off    | start            | end              |\n");
	//	fprintf(logFile,"| id   | reg    | pos | id     | starttime        | endtime          | fln | id     | id     | time             | time             |\n");
	//	for(tmp = Minimum(aircraftCrewPairXrefRoot); tmp; tmp = Successor(tmp)) {
	//		acpxPtr = (AircraftCrewPairXref *) getTreeObject(tmp);
	//		fprintf(logFile,"+------+--------+-----+--------+------------------+------------------+-----+--------+--------+------------------+------------------+\n");
	//		fprintf(logFile,"| %4d | %4d | %6d | %6d | %18d |\n",
	//			acpxPtr->crewPairID,
	//			acpxPtr->aircraftid,
	//			acpxPtr->crewid1,
	//			acpxPtr->crewid2,
	//			(int)acpxPtr->pairkey);
	//	}
	//}


	// fill in crewPairID in legList
	for(lPtr = legList; lPtr->aircraftID; lPtr++) {
		memset(&cdBuf,'\0',sizeof(cdBuf));
		cdBuf.aircraftid = lPtr->aircraftID;
		tmpDate_t = lPtr->schedOut;
		tmpDate = dt_time_tToDateTime(lPtr->schedOut);
		tmpDateE = dt_time_tToDateTime(lPtr->schedIn);
		lkRet = Lookup(acidStarttimeEndtimeCrewidRoot, &cdBuf, acidStarttimeEndtimeCrewidCmp, &tmp);
		switch(lkRet) {
		case EmptyTree:
		case GreaterThanLastItem:
		case ExactMatchFound: // impossible. we didn't enter entire key
			logMsg(logFile,"%s Line %d: aircraftid %d not found in crew assignment table. lkRet=%s\n", __FILE__,__LINE__,lPtr->aircraftID, bintreeRet[lkRet]);
			break;
		case NotFoundReturnedNextItem:
			for(;tmp;tmp = Successor(tmp)) {
				cdPtr = (CrewData *) getTreeObject(tmp);
				if(cdPtr->aircraftid != lPtr->aircraftID)
					break;
				//if(tmpDate >= cdPtr->starttime && tmpDate < cdPtr->endtime) { 
				//The above is the correct one, the following is a loose version. a flight leg intersects with duty, then it is covered
				  if(tmpDate <= cdPtr->endtime && tmpDateE >= cdPtr->starttime) { 
					memset(&acpxBuf,'\0',sizeof(acpxBuf));					
					acpxBuf.aircraftid = lPtr->aircraftID;
					acpxBuf.crewid1 = cdPtr->crewid;
					for(tmp = Successor(tmp);tmp;tmp = Successor(tmp)) {
						cdPtr = (CrewData *) getTreeObject(tmp);
						if(cdPtr->aircraftid != lPtr->aircraftID)
							break;
						if (acpxBuf.crewid1 == cdPtr->crewid)
								continue;
							//if(tmpDate >= cdPtr->starttime && tmpDate < cdPtr->endtime) { 
							//The above is the correct one, the following is a loose version. a flight leg intersects with duty, then it is covered
							if(tmpDate <= cdPtr->endtime && tmpDateE >= cdPtr->starttime) { 
								acpxBuf.crewid2 = cdPtr->crewid;
								acpxBuf.crewPairID = 0;
							if(acpxBuf.crewid1 < acpxBuf.crewid2)
								acpxBuf.pairkey = (((uint64) acpxBuf.crewid1) << 32) | acpxBuf.crewid2;
							else
								acpxBuf.pairkey = (((uint64) acpxBuf.crewid2) << 32) | acpxBuf.crewid1;
							tmp1 = TreeSearch(aircraftCrewPairXrefRoot, &acpxBuf, aircraftCrewPairXrefCmp);
							if(tmp1) {
								acpxPtr = (AircraftCrewPairXref *) getTreeObject(tmp1);
								lPtr->crewPairID = acpxPtr->crewPairID; //We may need to make sure the leg is actually in the pairTourS and pairTourE after the relaxation.
								break;
							}
						}
					}
				}
		    if (!tmp) break; // tmp may be already null, no need to continue the bigger for loop;
			}
			break;
		}
	}


	// display leg data with crewPairIDs
	logMsg(logFile,"\nLegs with crewPairIDs:\n");
	fprintf(logFile,"+----------+------------+------------+--------------+-------------+------------------+------------------+\n");
	fprintf(logFile,"| demandID | aircraftID | crewPairID | outAirportID | inAirportID | schedOut         | schedIn          |\n");
	fprintf(logFile,"+----------+------------+------------+--------------+-------------+------------------+------------------+\n");
	for(lPtr = legList; lPtr->aircraftID; lPtr++) {
		fprintf(logFile,"| %8d | %10d | %10d | %12d | %11d | %s | %s |\n",
			lPtr->demandID,
			lPtr->aircraftID,
			lPtr->crewPairID,
			lPtr->outAirportID,
			lPtr->inAirportID,
			(lPtr->schedOut) ?
	    			dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(lPtr->schedOut))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
			(lPtr->schedIn) ?
	    			dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(lPtr->schedIn))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
	}
	fprintf(logFile,"+----------+------------+------------+--------------+-------------+------------------+------------------+\n");
	fflush(logFile);

	//exit(0);
	return(0);
}



static CrewID *
textToCrewID(MYSQL_ROW row)
{
	CrewID *cidPtr;

	cidPtr = (CrewID *) calloc((size_t) 1, sizeof(CrewID));
	if(!cidPtr)
		return(NULL);
	if(!(cidPtr->crewid = atoi(row[CRWID_crewid]))) {
		logMsg(logFile,"%s Line %d, Bad data in textToCrewID().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cidPtr->employeenumber = strdup(row[CRWID_employeenumber]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToCrewID().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	cidPtr->baseairportid = (row[CRWID_baseairportid]) ? atoi(row[CRWID_baseairportid]) : 0;
	if(!(cidPtr->baseicao = strdup(row[CRWID_baseicao]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToCrewID().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cidPtr->fileas = strdup(row[CRWID_fileas]))) {
		logMsg(logFile,"%s Line %d, Bad data in textToCrewID().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	return(cidPtr);
}


static int
legCmp(void *a, void *b)
{
	int ret;
	LEG_DATA *a1 = (LEG_DATA *) a;
	LEG_DATA *b1 = (LEG_DATA *) b;
	if(a1->rec_outtime > b1->rec_outtime)
		return(1);
	if(a1->rec_outtime < b1->rec_outtime)
		return(-1);
	if((ret = strcmp(a1->registration, b1->registration)))
		return(ret);
	if((ret = strcmp(a1->outicao, b1->outicao)))
		return(ret);
	return(0);
}

static LEG_DATA *
textToLegData(MYSQL_ROW row)
{
	LEG_DATA *legPtr;
	int errNbr;
	int x; //05/23/08 ANG
	extern Demand *testFlightList; //05/23/08 ANG
	extern int countTestFlights; //05/23/08 ANG

	legPtr = (LEG_DATA *) calloc((size_t) 1, sizeof(LEG_DATA));
	if(!legPtr)
		return(NULL);
	if(!(legPtr->rowtype = strdup(row[LD_rowtype]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToLegData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	legPtr->rec_id = (row[LD_rec_id]) ? atoi(row[LD_rec_id]) : 0;
	legPtr->aircraftid = (row[LD_aircraftid]) ? atoi(row[LD_aircraftid]) : 0;
	if(!(legPtr->registration = strdup(row[LD_registration]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToLegData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	legPtr->demandid = (row[LD_demandid]) ? atoi(row[LD_demandid]) : 0;
	legPtr->outaptid = (row[LD_outaptid]) ? atoi(row[LD_outaptid]) : 0;
	if(!(legPtr->outicao = strdup(row[LD_outicao]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToLegData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	legPtr->inaptid = (row[LD_inaptid]) ? atoi(row[LD_inaptid]) : 0;
	if(!(legPtr->inicao = strdup(row[LD_inicao]))) {
		logMsg(logFile,"%s Line %d, in of Memory in textToLegData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	legPtr->outfboid =  (row[LD_outfboid]) ? atoi(row[LD_outfboid]) : 0;
	legPtr->infboid =  (row[LD_infboid]) ? atoi(row[LD_infboid]) : 0;

	if((legPtr->rec_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[LD_rec_outtime], NULL, &errNbr)) == BadDateTime) {
		logMsg(logFile,"%s Line %d, Bad date in textToLegData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[LD_rec_outtime]);
		writeWarningData(myconn); exit(1);
	}
	if((legPtr->rec_intime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[LD_rec_intime], NULL, &errNbr)) == BadDateTime) {
		logMsg(logFile,"%s Line %d, Bad date in textToLegData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[LD_rec_intime]);
		writeWarningData(myconn); exit(1);
	}
	legPtr->manuallyassigned = (row[LD_manuallyassigned]) ? atoi(row[LD_manuallyassigned]) : 0;

	//START - Lock test flight demands to corresponding aircraft - 05/23/08 ANG
	if (legPtr->manuallyassigned == 0){
		for(x = 0; x < countTestFlights; x++){
			if(testFlightList[x].demandID == legPtr->demandid){
				legPtr->manuallyassigned = 1;
				break;
			}
		}
	}
	//END - Lock test flight demands to corresponding aircraft - 05/23/08 ANG

	return(legPtr);
}

/*
typedef struct prepairconstraint {
	int ppcid;
	int crewid1;
	int crewid2;
	int priority;
	DateTime startdate;
	DateTime enddate;
	char *name1;
	char *name2;
	uint8 catIds;
} PrePairConstraint;

typedef enum {
	PPC_pairconstraintid, PPC_crewid1, PPC_emp1, PPC_crewname1, PPC_crewid2, PPC_emp2, PPC_crewname2, PPC_Check_Airman, PPC_Standards_Captain,
	PPC_Assistant_Chief_Pilot, PPC_Office_Management, PPC_Unrestricted, PPC_High_Minimums, PPC_Restricted, PPC_priority, PPC_startdate, PPC_enddate,
	PPC_nbr_items
} PPC;
*/
static void
textToPairConstraint(MYSQL_ROW row, PrePairConstraint *ppcPtr)
{
	int errNbr;
	char tbuf[32];

	ppcPtr->crewid1 = (row[PPC_crewid1]) ? atoi(row[PPC_crewid1]) : 0;
	ppcPtr->crewid2 = (row[PPC_crewid2]) ? atoi(row[PPC_crewid2]) : 0;
	ppcPtr->priority = (row[PPC_priority]) ? atoi(row[PPC_priority]) : 0;

	//if(optParam.downgrPairPriority1 == 1){
	//	if (ppcPtr->priority == 1)
	//		ppcPtr->priority = 2;
	//}

	strcpy(tbuf,row[PPC_startdate]);
	strcat(tbuf," 00:00");
	if((ppcPtr->startdate = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", tbuf, NULL, &errNbr)) == BadDateTime) {
		logMsg(logFile,"%s Line %d, Bad date in textToPairConstraint(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[PPC_startdate]);
		writeWarningData(myconn); exit(1);
	}

	strcpy(tbuf,row[PPC_enddate]);
	strcat(tbuf," 00:00");
	if((ppcPtr->enddate = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", tbuf, NULL, &errNbr)) == BadDateTime) {
		logMsg(logFile,"%s Line %d, Bad date in textToPairConstraint(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[PPC_enddate]);
		writeWarningData(myconn); exit(1);
	}
	strcpy(ppcPtr->name1,(row[PPC_crewname1]) ? row[PPC_crewname1] : "");
	strcpy(ppcPtr->name2,(row[PPC_crewname2]) ? row[PPC_crewname2] : "");
	if(row[PPC_Check_Airman][0] == 'Y')
		setBit(ppcPtr->catIds,Bit_Check_Airman);
	if(row[PPC_Standards_Captain][0] == 'Y')
		setBit(ppcPtr->catIds,Bit_Standards_Captain);
	if(row[PPC_Assistant_Chief_Pilot][0] == 'Y')
		setBit(ppcPtr->catIds,Bit_Assistant_Chief_Pilot);
	if(row[PPC_Office_Management][0] == 'Y')
		setBit(ppcPtr->catIds,Bit_Office_Management);
	if(row[PPC_Unrestricted][0] == 'Y')
		setBit(ppcPtr->catIds,Bit_Unrestricted);
	if(row[PPC_High_Minimums][0] == 'Y')
		setBit(ppcPtr->catIds,Bit_High_Minimums);
	if(row[PPC_Restricted][0] == 'Y')
		setBit(ppcPtr->catIds,Bit_Restricted);
}
static int
pcCrewIdToCrewIdCmp(void *a1, void *b1)
{
	PairConstraint *a = (PairConstraint *) a1;
	PairConstraint *b = (PairConstraint *) b1;
	int ret;

	if((ret = a->crew1ID - b->crew1ID))
		return(ret);
	if((ret = a->crew2ID - b->crew2ID))
		return(ret);
//	if((ret = a->priority - b->priority))
//		return(ret);
	return((int) a->startTm - (int) b->startTm);
}

static int
pcCrewIdToCategoryCmp(void *a1, void *b1)
{
	PairConstraint *a = (PairConstraint *) a1;
	PairConstraint *b = (PairConstraint *) b1;
	int ret;

	if((ret = a->crew1ID - b->crew1ID))
		return(ret);
	if((ret = a->categoryID - b->categoryID))
		return(ret);
//	if((ret = a->priority - b->priority))
//		return(ret);
	return((int) a->startTm - (int) b->startTm);
}


static CrewData *
textToCrewData(MYSQL_ROW row)
{
	CrewData *cdPtr;
	int errNbr;

	cdPtr = (CrewData *) calloc((size_t) 1, sizeof(CrewData));
	if(!cdPtr)
		return(NULL);
	if(!(cdPtr->employeenumber = strdup(row[CRW_employeenumber]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->aircraftTypeName = strdup(row[CRW_aircraftTypeName]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->aircraftTypeID = atoi(row[CRW_aircraftTypeID]))) {
		logMsg(logFile,"%s Line %d, zero aircraftTypeID in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if((cdPtr->starttime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[CRW_starttime], NULL, &errNbr)) == BadDateTime) {
		logMsg(logFile,"%s Line %d, Bad date in textToCrewData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[CRW_starttime]);
		writeWarningData(myconn); exit(1);
	}
	if((cdPtr->endtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[CRW_endtime], NULL, &errNbr)) == BadDateTime) {
		logMsg(logFile,"%s Line %d, Bad date in textToCrewData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[CRW_endtime]);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->crewid = atoi(row[CRW_crewid]))) {
		logMsg(logFile,"%s Line %d, zero crewid in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->fileas = strdup(row[CRW_fileas]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->crewassignmentid = atoi(row[CRW_crewassignmentid]))) {
		logMsg(logFile,"%s Line %d, zero crewassignmentid in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->aircraftid = atoi(row[CRW_aircraftid]))) {
		logMsg(logFile,"%s Line %d, zero aircraftid in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->registration = strdup(row[CRW_registration]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->position = atoi(row[CRW_position]))) {
		logMsg(logFile,"%s Line %d, zero position in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->scenarioid = atoi(row[CRW_scenarioid]))) {
		logMsg(logFile,"%s Line %d, zero scenarioid in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	return(cdPtr);
}

static CS_CrewData *
textToCsCrewData(MYSQL_ROW row)
{
	CS_CrewData *cdPtr;
	int errNbr;

	cdPtr = (CS_CrewData *) calloc((size_t) 1, sizeof(CS_CrewData));
	if(!cdPtr)
		return(NULL);
	if(!(cdPtr->crewid = atoi(row[CS_CRW_crewid]))) {
		logMsg(logFile,"%s Line %d, zero aircraftTypeID in textToCsCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if((cdPtr->starttime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[CS_CRW_starttime], NULL, &errNbr)) == BadDateTime) {
		logMsg(logFile,"%s Line %d, Bad date in textToCsCrewData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[CS_CRW_starttime]);
		writeWarningData(myconn); exit(1);
	}
	if((cdPtr->endtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[CS_CRW_endtime], NULL, &errNbr)) == BadDateTime) {
		logMsg(logFile,"%s Line %d, Bad date in textToCsCrewData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[CS_CRW_endtime]);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->ca_aircraftid = atoi(row[CS_CRW_ca_aircraftid]))) {
		logMsg(logFile,"%s Line %d, zero ca_aircraftTypeID in textToCsCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->cd_aircraftid = atoi(row[CS_CRW_cd_aircraftid]))) {
		logMsg(logFile,"%s Line %d, zero cd_aircraftTypeID in textToCsCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->ca_registration = strdup(row[CS_CRW_ca_registration]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->cd_registration = strdup(row[CS_CRW_cd_registration]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(cdPtr->position = atoi(row[CS_CRW_position]))) {
		logMsg(logFile,"%s Line %d, zero position in textToCsCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(((!oracleDirect && row[CS_CRW_scheduled_on]) || (oracleDirect && row[CS_CRW_scheduled_on][0])) && strcmp(row[CS_CRW_scheduled_on], "1901/01/01 00:00") != 0) {
		if((cdPtr->scheduled_on = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[CS_CRW_scheduled_on], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToCsCrewData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[CS_CRW_scheduled_on]);
			writeWarningData(myconn); exit(1);
		}
	}
	if(((!oracleDirect && row[CS_CRW_scheduled_off]) || (oracleDirect && row[CS_CRW_scheduled_off][0])) && strcmp(row[CS_CRW_scheduled_off], "1901/01/01 00:00") != 0) {
		if((cdPtr->scheduled_off = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[CS_CRW_scheduled_off], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToCsCrewData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[CS_CRW_scheduled_off]);
			writeWarningData(myconn); exit(1);
		}
	}
	if(((!oracleDirect && row[CS_CRW_actual_on]) || (oracleDirect && row[CS_CRW_actual_on][0])) && strcmp(row[CS_CRW_actual_on], "1901/01/01 00:00") != 0) {
		if((cdPtr->actual_on = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[CS_CRW_actual_on], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToCsCrewData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[CS_CRW_actual_on]);
			writeWarningData(myconn); exit(1);
		}
	}
	if(((!oracleDirect && row[CS_CRW_actual_off]) || (oracleDirect && row[CS_CRW_actual_off][0])) && strcmp(row[CS_CRW_actual_off], "1901/01/01 00:00") != 0) {
		if((cdPtr->actual_off = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[CS_CRW_actual_off], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToCsCrewData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[CS_CRW_actual_off]);
			writeWarningData(myconn); exit(1);
		}
	}
	if(((!oracleDirect && row[CS_CRW_currdate]) || (oracleDirect && row[CS_CRW_currdate][0])) && strcmp(row[CS_CRW_currdate], "1901/01/01 00:00") != 0) {
		if((cdPtr->currdate = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[CS_CRW_currdate], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToCsCrewData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[CS_CRW_currdate]);
			writeWarningData(myconn); exit(1);
		}
	}
	if((!oracleDirect && row[CS_CRW_lastupdated]) || (oracleDirect && row[CS_CRW_lastupdated][0])) {
		if((cdPtr->lastupdated = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[CS_CRW_lastupdated], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToCsCrewData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[CS_CRW_lastupdated]);
			writeWarningData(myconn); exit(1);
		}
	}
	if(!(cdPtr->scenarioid = atoi(row[CS_CRW_scenarioid]))) {
		logMsg(logFile,"%s Line %d, zero scenarioid in textToCsCrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

		//populate the scheduled times if the actual times are ready.  //Roger 02/20/2007
	cdPtr->scheduled_on = cdPtr->actual_on ? cdPtr->actual_on : cdPtr->scheduled_on;
	cdPtr->scheduled_off = cdPtr->actual_off ? cdPtr->actual_off : cdPtr->scheduled_off;


	// populate the actual on/off if the scheduled on/off is before  the planning window 
	pwStartDate = dt_time_tToDateTime(optParam.windowStart);
	if (!cdPtr->actual_on && cdPtr->scheduled_on <=	pwStartDate)
		cdPtr->actual_on = cdPtr->scheduled_on;
	
	if (!cdPtr->actual_off && cdPtr->scheduled_off <= pwStartDate)
		cdPtr->actual_off = cdPtr->scheduled_off;



	return(cdPtr);
}

static RawAircraftData *
textToRawAircraftData(MYSQL_ROW row)
{
	RawAircraftData *radPtr;
	int errNbr;

	radPtr = (RawAircraftData *) calloc((size_t) 1, sizeof(RawAircraftData));
	if(!radPtr)
		return(NULL);

	if(!(radPtr->rowtype = strdup(row[whereRowtype]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToRawAircraftData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(row[whereRec_id]) {
		if(!(radPtr->recid = atoi(row[whereRec_id]))) {
			logMsg(logFile,"%s Line %d, zero recid in textToRawAircraftData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereAircraftid]) {
		if(!(radPtr->aircraftid = atoi(row[whereAircraftid]))) {
			logMsg(logFile,"%s Line %d, zero aircraftid in textToRawAircraftData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(!(radPtr->registration = strdup(row[whereRegistration]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToRawAircraftData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(row[whereAc_type]) {
		if(!(radPtr->ac_type = atoi(row[whereAc_type]))) {
			logMsg(logFile,"%s Line %d, zero ac_type in textToRawAircraftData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereDemandID]) {
		if(!(radPtr->demandid = atoi(row[whereDemandID]))) {
			logMsg(logFile,"%s Line %d, zero demandid in textToRawAircraftData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereOutaptid]) {
		if(!(radPtr->outaptid = atoi(row[whereOutaptid]))) {
			logMsg(logFile,"%s Line %d, zero outaptid in textToRawAircraftData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereOuticao]) {
		if(!(radPtr->outicao = strdup(row[whereOuticao]))) {
			logMsg(logFile,"%s Line %d, Out of Memory in textToRawAircraftData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereInaptid]) {
		if(!(radPtr->inaptid = atoi(row[whereInaptid]))) {
			logMsg(logFile,"%s Line %d, zero inaptid in textToRawAircraftData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereInicao]) {
		if(!(radPtr->inicao = strdup(row[whereInicao]))) {
			logMsg(logFile,"%s Line %d, out of Memory in textToRawAircraftData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereOutfboid]) {
		if(!(radPtr->outfboid = atoi(row[whereOutfboid]))) {
			logMsg(logFile,"%s Line %d, zero outfboid in textToRawAircraftData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereInfboid]) {
		if(!(radPtr->infboid = atoi(row[whereInfboid]))) {
			logMsg(logFile,"%s Line %d, zero infboid in textToRawAircraftData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereRec_outtime]) {
		if((radPtr->rec_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[whereRec_outtime], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToRawAircraftData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[whereRec_outtime]);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereRec_intime]) {
		if((radPtr->rec_intime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[whereRec_intime], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToRawAircraftData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[whereRec_intime]);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereActualout]) {
		if((radPtr->actualout = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[whereActualout], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToRawAircraftData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[whereActualout]);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereActualoff]) {
		if((radPtr->actualoff = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[whereActualoff], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToRawAircraftData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[whereActualoff]);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereActualon]) {
		if((radPtr->actualon = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[whereActualon], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToRawAircraftData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[whereActualon]);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereActualin]) {
		if((radPtr->actualin = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[whereActualin], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in textToRawAircraftData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[whereActualin]);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereSequenceposition]) {
		if(!(radPtr->sequenceposition = atoi(row[whereSequenceposition]))) {
			logMsg(logFile,"%s Line %d, zero sequenceposition in textToRawAircraftData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(row[whereCrewnotified]) {
		radPtr->crewnotified = atoi(row[whereCrewnotified]);
	}

	//populate the scheduled times if the actual times are ready.  //Roger 02/20/2007, use IFNULL is query (04/10/08)
	//radPtr->rec_outtime = radPtr->actualout ? radPtr->actualout : radPtr->rec_outtime;
	//radPtr->rec_intime = radPtr->actualin ? radPtr->actualin : radPtr->rec_intime;
	return(radPtr);
}

static SS_CrewData *
textToSS_CrewData(MYSQL_ROW row)
{
	SS_CrewData *ssCdPtr;
	int errNbr;
	char tbuf1[8192];
	char tbuf2[8192];
	char *wptrs[256];


	ssCdPtr = (SS_CrewData *) calloc((size_t) 1, sizeof(SS_CrewData));
	if(!ssCdPtr)
		return(NULL);

	if(!(ssCdPtr->zbadgeid = strdup(row[SS_zbadgeid]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(ssCdPtr->zdeptdesc = strdup(row[SS_zdeptdesc]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if((ssCdPtr->dtdate = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[SS_dtdate], NULL, &errNbr)) == BadDateTime) {
		logMsg(logFile,"%s Line %d, Bad date in textToSS_CrewData(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[SS_dtdate]);
		writeWarningData(myconn); exit(1);
	}
	if(!(ssCdPtr->zlname = strdup(row[SS_zlname]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(ssCdPtr->zfname = strdup(row[SS_zfname]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if((! oracleDirect && row[SS_zmname]) || (oracleDirect && row[SS_zmname][0])) {
		if(!(ssCdPtr->zmname = strdup(row[SS_zmname]))) {
			logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	if(!(ssCdPtr->lempid = atoi(row[SS_lempid]))) {
		logMsg(logFile,"%s Line %d, zero lempid in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(ssCdPtr->lempinfoid = atoi(row[SS_lempinfoid]))) {
		logMsg(logFile,"%s Line %d, zero lempinfoid in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(ssCdPtr->lpostid = atoi(row[SS_lpostid]))) {
		logMsg(logFile,"%s Line %d, zero lpostid in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(ssCdPtr->zpostdesc = strdup(row[SS_zpostdesc]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

/*
	if(!(ssCdPtr->zshiftdesc = strdup(row[SS_zshiftdesc]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
*/
// fix: this fix goes with new SQL to read Oracle ScheduleSoft data.
// added a (+) to the shift-empjobs join in the ssoftcrew.sql file as follows:
// "and sh.lshiftid(+) = ej.lshiftid"
	if((! oracleDirect && row[SS_zshiftdesc]) || (oracleDirect && row[SS_zshiftdesc][0])) {
		if(!(ssCdPtr->zshiftdesc = strdup(row[SS_zshiftdesc]))) {
			logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	else {
		if(strncasecmp(ssCdPtr->zpostdesc,"Assistant", strlen("Assistant")) == 0 || strncasecmp(ssCdPtr->zpostdesc,"Check", strlen("Check")) == 0 ||
		    strncasecmp(ssCdPtr->zpostdesc,"Standards", strlen("Standards")) == 0)
			ssCdPtr->zshiftdesc = strdup("PIC");
		else
			ssCdPtr->zshiftdesc = strdup("SIC");
	}
// end fix
	if(!(ssCdPtr->zacccodeid = strdup(row[SS_zacccodeid]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(ssCdPtr->zacccodedesc = strdup(row[SS_zacccodedesc]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if((! oracleDirect && row[SS_znote]) || (oracleDirect && row[SS_znote][0])) {
		char *p;
		(void) hexToString(row[SS_znote], tbuf1, sizeof(tbuf1) -1);
		p = tbuf1;
		while(*p) {
			if(*p == '\t' || *p == '\r' || *p == '\n')
				*p = ' ';
			++p;
		}
		(void) rmvExtraSpaces(tbuf1, tbuf2, wptrs);
		if(!(ssCdPtr->znote = strdup(tbuf2))) {
			logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}

	return(ssCdPtr);
}

static SS_CrewData *
crewDataToSS_CrewData(SS_CrewData *ssCdPtr0, CrewData *cdPtr)
{
	SS_CrewData *ssCdPtr;
	DateTime tmpDate;
	int timeZoneAdjMinutes;

	ssCdPtr = (SS_CrewData *) calloc((size_t) 1, sizeof(SS_CrewData));
	if(!ssCdPtr)
		return(NULL);

	if(!(ssCdPtr->zbadgeid = strdup(cdPtr->employeenumber))) {
		logMsg(logFile,"%s Line %d, Out of Memory in crewDataToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(!(ssCdPtr->zdeptdesc = strdup(cdPtr->aircraftTypeName))) {
		logMsg(logFile,"%s Line %d, Out of Memory in crewDataToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	//ssCdPtr->dtdate = dtdate;
	tmpDate = cdPtr->starttime & DateOnly;
	timeZoneAdjMinutes = optParam.crewTourStartInMin + locFromGMTinMinutes + timeZoneAdjByApt(ssCdPtr0->baseAirportID, tmpDate)/60;

	tmpDate = dt_addToDateTime(Minutes, -timeZoneAdjMinutes, cdPtr->starttime) & DateOnly;  //Offset to local time.
	//ssCdPtr->dtdate = dt_addToDateTime(Minutes, optParam.crewTourStartInMin + locFromGMTinMinutes + timeZoneAdjByApt(ssCdPtr0->baseAirportID, tmpDate)/60, tmpDate);
	ssCdPtr->dtdate = dt_addToDateTime(Minutes, timeZoneAdjMinutes, tmpDate);
	ssCdPtr->zlname = "Rec not in SS";
	ssCdPtr->zfname = "";
	ssCdPtr->zmname = "";
	ssCdPtr->lempid = 0;
	ssCdPtr->lempinfoid = 0;
	ssCdPtr->lpostid = 0;
	//ssCdPtr->zpostdesc = "Rec not in SS";
	ssCdPtr->zpostdesc = "Unrestricted";
	if(cdPtr->position == 1)
		ssCdPtr->zshiftdesc = "PIC";
	else
		ssCdPtr->zshiftdesc = "SIC";
	ssCdPtr->zacccodeid = "";
	ssCdPtr->zacccodedesc = "";
	ssCdPtr->znote = "No Schedule Soft record.";
	ssCdPtr->baseAirportID = ssCdPtr0->baseAirportID;
	ssCdPtr->crewID = ssCdPtr0->crewID;
	return(ssCdPtr);
}

static int
empnbrCmp(void *a1, void *b1)
{
	CrewID *a = (CrewID *) a1;
	CrewID *b = (CrewID *) b1;

	return(strcasecmp(a->employeenumber, b->employeenumber));


}

static int
crewidCmp(void *a1, void *b1)
{
	CrewID *a = (CrewID *) a1;
	CrewID *b = (CrewID *) b1;

	return(a->crewid - b->crewid);
}

static int
integerCmp(void *a1, void *b1)
{
	int *a = (int *) a1;
	int *b = (int *) b1;

	return(*a - *b);
}

static int
charterStatsCmp(void *a1, void *b1)
{
	CharterStats *a = (CharterStats *) a1;
	CharterStats *b = (CharterStats *) b1;

	return(a->ownerid - b->ownerid);
}

static int
crewListCrewidCmp(void *a1, void *b1)
{
	Crew *a = (Crew *) a1;
	Crew *b = (Crew *) b1;

	return(a->crewID - b->crewID);
}

static int
empnbrStarttimeActypRegCmp(void *a1, void *b1)
{
	CrewData *a = (CrewData *) a1;
	CrewData *b = (CrewData *) b1;
	int ret;

	if((ret = strcmp(a->employeenumber, b->employeenumber)))
		return(ret);
	if(a->starttime > b->starttime)
		return(1);
	if(a->starttime < b->starttime)
		return(-1);
	if((ret = strcmp(a->aircraftTypeName, b->aircraftTypeName)))
		return(ret);
	if((ret = strcmp(a->registration, b->registration)))
		return(ret);
	return(0);
}

static int
cs_crew_data_crewidScheduledOnStarttimeCmp(void *a1, void *b1)
{
	CS_CrewData *a = (CS_CrewData *) a1;
	CS_CrewData *b = (CS_CrewData *) b1;
	int ret;

	ret = a->crewid - b->crewid;
	if(ret)
		return(ret);

	if(a->lastupdated > b->lastupdated)
		return(1);
	if(a->lastupdated < b->lastupdated)
		return(-1);

	if(! a->scheduled_on && ! b->scheduled_on) {
		if(a->actual_on > b->actual_on)
			return(1);
		if(a->actual_on < b->actual_on)
			return(-1);

	}
	else if(! a->scheduled_on &&  b->scheduled_on) {
		if(a->actual_on > b->scheduled_on)
			return(1);
		if(a->actual_on < b->scheduled_on)
			return(-1);
	}
	else if(a->scheduled_on &&  ! b->scheduled_on) {
		if(a->scheduled_on > b->actual_on)
			return(1);
		if(a->scheduled_on < b->actual_on)
			return(-1);
	}
	else if(a->scheduled_on &&  b->scheduled_on) {
		if(a->scheduled_on > b->scheduled_on)
			return(1);
		if(a->scheduled_on < b->scheduled_on)
			return(-1);
	}

	if(a->starttime > b->starttime)
		return(1);
	if(a->starttime < b->starttime)
		return(-1);
	if(a->endtime > b->endtime)
		return(1);
	if(a->endtime < b->endtime)
		return(-1);
	if(a->ca_aircraftid > b->ca_aircraftid)
		return(1);
	if(a->ca_aircraftid < b->ca_aircraftid)
		return(-1);
	
	//12/13/2007 RLZ: To deal with the bad data in cs_crew_duty table  
    if(a->cd_aircraftid > b->cd_aircraftid)  
        return(1);  
    if(a->cd_aircraftid < b->cd_aircraftid)  
        return(-1);   
	return(0);
}

static int
crewidAcidStarttimeCmp(void *a1, void *b1)
{
	CrewData *a = (CrewData *) a1;
	CrewData *b = (CrewData *) b1;

	if(a->crewid > b->crewid)
		return(1);
	if(a->crewid < b->crewid)
		return(-1);
	if(a->aircraftid > b->aircraftid)
		return(1);
	if(a->aircraftid < b->aircraftid)
		return(-1);
	if(a->starttime > b->starttime)
		return(1);
	if(a->starttime < b->starttime)
		return(-1);
	return(0);
}

//static int
//aircraftCrewPairXrefCmp(void *a1, void *b1)
//{
//	AircraftCrewPairXref *a = (AircraftCrewPairXref *) a1;
//	AircraftCrewPairXref *b = (AircraftCrewPairXref *) b1;
//	int ret;
//
//	if((ret = a->aircraftid - b->aircraftid))
//		return(ret);
//	if(a->starttime < b->starttime)
//		return(-1);
//	if(a->starttime > b->starttime)
//		return(1);
//	if(a->endtime < b->endtime)
//		return(-1);
//	if(a->endtime > b->endtime)
//		return(1);
//	return(a->crewid - b->crewid);
//}

static int
aircraftCrewPairXrefCmp(void *a1, void *b1)
{
	AircraftCrewPairXref *a = (AircraftCrewPairXref *) a1;
	AircraftCrewPairXref *b = (AircraftCrewPairXref *) b1;
	int ret;

	if((ret = a->aircraftid - b->aircraftid))
		return(ret);
	//return(a->pairkey - b->pairkey);
    //RLZ 02/27/2008: The above line can not be used to compare unit64. 

	if (a->pairkey > b->pairkey)
		return 1;
	else if (a->pairkey < b->pairkey)
		return -1;
	else return 0;
	//return ( a->crewPairID - b->crewPairID);	
}

static int
acidStarttimeEndtimeCrewidCmp(void *a1, void *b1)
{
	CrewData *a = (CrewData *) a1;
	CrewData *b = (CrewData *) b1;
	int ret;

	if((ret = a->aircraftid - b->aircraftid))
		return(ret);
	if(a->starttime < b->starttime)
		return(-1);
	if(a->starttime > b->starttime)
		return(1);
	if(a->endtime < b->endtime)
		return(-1);
	if(a->endtime > b->endtime)
		return(1);
	return(a->crewid - b->crewid);
}

/**********************************/
static int
crewidStarttimeEndtimeAcidCmp(void *a1, void *b1)
{
	CrewData *a = (CrewData *) a1;
	CrewData *b = (CrewData *) b1;
//	int ret;

	if(a->crewid < b->crewid)
		return(-1);
	if(a->crewid > b->crewid)
		return(1);
	if(a->starttime < b->starttime)
		return(-1);
	if(a->starttime > b->starttime)
		return(1);
	if(a->endtime < b->endtime)
		return(-1);
	if(a->endtime > b->endtime)
		return(1);
	return(a->aircraftid - b->aircraftid);
}
/**********************************/  //Jintao's change 10/01/2007

static int
starttimeCrewidCmp(void *a1, void *b1)
{
	CrewData *a = (CrewData *) a1;
	CrewData *b = (CrewData *) b1;

	if(a->starttime > b->starttime)
		return(1);
	if(a->starttime < b->starttime)
		return(-1);
	if(a->crewid > b->crewid)
		return(1);
	if(a->crewid < b->crewid)
		return(-1);
	return(0);
}

static int
preCrewPairCmp(void *a1, void *b1)
{
	PRE_Crew_Pair *a = (PRE_Crew_Pair *) a1;
	PRE_Crew_Pair *b = (PRE_Crew_Pair *) b1;

	if(a->aircraftid > b->aircraftid)
		return(1);
	if(a->aircraftid < b->aircraftid)
		return(-1);
	return(0);
}

static int
crewPairCmp(void *a1, void *b1)
{
	CrewPair *a = (CrewPair *) a1;
	CrewPair *b = (CrewPair *) b1;

	if(a->crewPairID > b->crewPairID)
		return(1);
	if(a->crewPairID < b->crewPairID)
		return(-1);
	return(0);
}

static int
crewPairACCmp(void *a1, void *b1)
{
	CrewPair *a = (CrewPair *) a1;
	CrewPair *b = (CrewPair *) b1;
	int ret;

	if ((ret = *(a->aircraftID) - *(b->aircraftID)))
		return ret;
	if ((ret = a->hasFlownFirst  - b->hasFlownFirst))
		return ret;
	return (a->crewPairID - b->crewPairID);
}


static int
pxCmp(void *a1, void *b1)
{
	CrewPairX *a = (CrewPairX *) a1;
	CrewPairX *b = (CrewPairX *) b1;

	if(a->assignedAircraftID > b->assignedAircraftID)
		return(1);
	if(a->assignedAircraftID < b->assignedAircraftID)
		return(-1);
	if(a->crewPairID > b->crewPairID)
		return(1);
	if(a->crewPairID < b->crewPairID)
		return(-1);
	return(0);
}

static int
pxPairCmp(void *a1, void *b1)
{
	CrewPairX *a = (CrewPairX *) a1;
	CrewPairX *b = (CrewPairX *) b1;
	//int ret;
	if(a->pairkey > b->pairkey)
		return(1);
	if(a->pairkey < b->pairkey)
		return(-1);
	/*
	if( ret = (a->assignedAircraftID - b->assignedAircraftID))	
		if((ret = a->assignedAircraftID - b->assignedAircraftID))
		return(ret);
	return(0);
	*/
	if(Max(a->crewMemA_starttime,a->crewMemB_starttime) > Max(b->crewMemA_starttime,b->crewMemB_starttime)) 
		return(1);
	if(Max(a->crewMemA_starttime,a->crewMemB_starttime) < Max(b->crewMemA_starttime,b->crewMemB_starttime))
		return(-1);
	return(a->assignedAircraftID - b->assignedAircraftID);
}

static int
pxPairDupChkCmp(void *a1, void *b1)
{
	CrewPairX *a = (CrewPairX *) a1;
	CrewPairX *b = (CrewPairX *) b1;
	int ret;

	if(a->pairkey > b->pairkey)
		return(1);
	if(a->pairkey < b->pairkey)
		return(-1);
	if((ret = a->assignedAircraftID - b->assignedAircraftID))
		return(ret);
	if(a->pairStartTm > b->pairStartTm)
		return(1);
	if(a->pairStartTm < b->pairStartTm)
		return(-1);
	if(a->pairEndTm > b->pairEndTm)
		return(1);
	if(a->pairEndTm < b->pairEndTm)
		return(-1);
	return(0);
}

static int
badgeDeptDateCmp(void *a1, void *b1)
{
	SS_CrewData *a = (SS_CrewData *) a1;
	SS_CrewData *b = (SS_CrewData *) b1;
	int ret;

	if((ret = strcmp(a->zbadgeid, b->zbadgeid)))
		return(ret);
	if((ret = strcmp(a->zdeptdesc, b->zdeptdesc)))
		return(ret);
	if(a->dtdate > b->dtdate)
		return(1);
	if(a->dtdate < b->dtdate)
		return(-1);
	return(0);
}

static int
bwWorkCmp(void *a1, void *b1)
{
	CrewData *a = (CrewData *) a1;
	CrewData *b = (CrewData *) b1;
	int ret;

	if(a->starttime > b->starttime)
		return(1);
	if(a->starttime < b->starttime)
		return(-1);
	if((ret = strcmp(a->registration, b->registration)))
		return(ret);
	return(a->position - b->position);
}

static int
dateListCmp(void *a1, void *b1)
{
	DateTime a = *(DateTime *) a1;
	DateTime b = *(DateTime *) b1;

	if(a > b)
		return(1);
	if(a < b)
		return(-1);
	return(0);
}

static void
showRawAircraftDataHeader(void)
{
	fprintf(logFile,"+-----------+----------+------+--------+------+--------+--------+--------+--------+--------+------------------+------------------+");
	fprintf(logFile,"------------------+------------------+------------------+------------------+-----+------+\n");

	fprintf(logFile,"|           |          | acft |        |   ac | demand | outapt | out    |  inapt | in     |                  |                  |");
	fprintf(logFile,"                  |                  |                  |                  | seq |      |\n");

	fprintf(logFile,"| rowtype   |    recid |   id | reg    | type |     id |     id | icao   |     id | icao   | rec_outtime      | rec_intime       |");
	fprintf(logFile," actualout        | actualoff        | actualon         | actualin         | pos |  gap |\n");


	fprintf(logFile,"+-----------+----------+------+--------+------+--------+--------+--------+--------+--------+------------------+------------------+");
	fprintf(logFile,"------------------+------------------+------------------+------------------+-----+------+\n");
}

static void
showRawAircraftDataFooter(void)
{
	fprintf(logFile,"+-----------+----------+------+--------+------+--------+--------+--------+--------+--------+------------------+------------------+");
	fprintf(logFile,"------------------+------------------+------------------+------------------+-----+------+\n\n");

}

static void
showRawAircraftData(RawAircraftData *radPtr)
{
	char dbuf1[32];
	char dbuf2[32];
	char dbuf3[32];
	char dbuf4[32];
	char dbuf5[32];
	char dbuf6[32];

	fprintf(logFile,"| %-9s | %8d | %4d | %-6s | %4d | %6d | %6d | %-6s | %6d | %-6s | %s | %s | %s | %s | %s | %s | %3d | %4d |\n",
    		radPtr->rowtype,
    		radPtr->recid,
    		radPtr->aircraftid,
    		radPtr->registration,
    		radPtr->ac_type,
    		radPtr->demandid,
    		radPtr->outaptid,
    		(radPtr->outicao) ? radPtr->outicao : "",
    		radPtr->inaptid,
    		(radPtr->inicao) ? radPtr->inicao : "",
    		(radPtr->rec_outtime) ? dt_DateTimeToDateTimeString(radPtr->rec_outtime, dbuf1, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
    		(radPtr->rec_intime) ? dt_DateTimeToDateTimeString(radPtr->rec_intime, dbuf2, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
    		(radPtr->actualout) ? dt_DateTimeToDateTimeString(radPtr->actualout, dbuf3, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
    		(radPtr->actualoff) ? dt_DateTimeToDateTimeString(radPtr->actualoff, dbuf4, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
    		(radPtr->actualon) ? dt_DateTimeToDateTimeString(radPtr->actualon, dbuf5, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
    		(radPtr->actualin) ? dt_DateTimeToDateTimeString(radPtr->actualin, dbuf6, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
		radPtr->sequenceposition,
		radPtr->minutes_since_previous_landing);
}

static void showTravelLeg(CsTravelData *cstrlPtr){								
	char dbuf1[32];
	char dbuf2[32];

	fprintf(logFile,"| %8d | %6d | %6d | %s | %s | \n",
		cstrlPtr->crewID,
		cstrlPtr->dpt_aptID,
		cstrlPtr->arr_aptID,
		(cstrlPtr->travel_dptTm) ? dt_DateTimeToDateTimeString(cstrlPtr->travel_dptTm, dbuf1, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
		(cstrlPtr->travel_arrTm) ? dt_DateTimeToDateTimeString(cstrlPtr->travel_arrTm, dbuf2, "%Y/%m/%d %H:%M") : "0000/00/00 00:00");
}


static void
showCrewDataNotInSsHeader(char *msg)
{
	fprintf(logFile,"%s\n", msg);
	fprintf(logFile,"+----------+-----------+------------------+------------------+--------+--------------------------------+---------+------+--------+-----+------+------------+\n");
	fprintf(logFile,"| employee |           |                  |                  |        |                                | crew    |   ac |        |     | scen | Date not   |\n");
	fprintf(logFile,"| number   | ac type   | start time       | end time         | crewid | fileas                         | asgn id |   id | reg    | pos |   id | in SS      |\n");
	fprintf(logFile,"+----------+-----------+------------------+------------------+--------+--------------------------------+---------+------+--------+-----+------+------------+\n");
}

static void
showCrewDataNotInSsFooter(void)
{
	fprintf(logFile,"+----------+-----------+------------------+------------------+--------+--------------------------------+---------+------+--------+-----+------+------------+\n");
	fprintf(logFile,"\n\n\n\n");
}

static void
showCrewDataNotInSs(CrewData *cdPtr, DateTime ssdt)
{
	char dbuf1[32];
	char dbuf2[32];
	char dbuf3[32];

	fprintf(logFile,"| %-8s | %-9s | %s | %s | %6d | %-30s | %7d | %4d | %-6s | %3d | %4d | %s |\n",
		cdPtr->employeenumber,
		cdPtr->aircraftTypeName,
		dt_DateTimeToDateTimeString(cdPtr->starttime, dbuf1, "%Y/%m/%d %H:%M"),
		dt_DateTimeToDateTimeString(cdPtr->endtime, dbuf2, "%Y/%m/%d %H:%M"),
		cdPtr->crewid,
		cdPtr->fileas,
		cdPtr->crewassignmentid,
		cdPtr->aircraftid,
		cdPtr->registration,
		cdPtr->position,
		cdPtr->scenarioid,
		dt_DateTimeToDateTimeString(ssdt, dbuf3, "%Y/%m/%d"));
}

static void
showCrewDataHeader(char *msg)
{
	fprintf(logFile,"%s\n", msg);
	fprintf(logFile,"+----------+-----------+------------------+------------------+--------+--------------------------------+---------+------+--------+-----+------+\n");
	fprintf(logFile,"| employee |           |                  |                  |        |                                | crew    |   ac |        |     | scen |\n");
	fprintf(logFile,"| number   | ac type   | start time       | end time         | crewid | fileas                         | asgn id |   id | reg    | pos |   id |\n");
	fprintf(logFile,"+----------+-----------+------------------+------------------+--------+--------------------------------+---------+------+--------+-----+------+\n");
}

static void
showCrewDataFooter(void)
{
	fprintf(logFile,"+----------+-----------+------------------+------------------+--------+--------------------------------+---------+------+--------+-----+------+\n");
	fprintf(logFile,"\n\n\n\n");
}

static void
showCrewData(CrewData *cdPtr)
{
	char dbuf1[32];
	char dbuf2[32];

	fprintf(logFile,"| %-8s | %-9s | %s | %s | %6d | %-30s | %7d | %4d | %-6s | %3d | %4d |\n",
		cdPtr->employeenumber,
		cdPtr->aircraftTypeName,
		dt_DateTimeToDateTimeString(cdPtr->starttime, dbuf1, "%Y/%m/%d %H:%M"),
		dt_DateTimeToDateTimeString(cdPtr->endtime, dbuf2, "%Y/%m/%d %H:%M"),
		cdPtr->crewid,
		cdPtr->fileas,
		cdPtr->crewassignmentid,
		cdPtr->aircraftid,
		cdPtr->registration,
		cdPtr->position,
		cdPtr->scenarioid);
}

static void
showCsCrewDataHeader(char *msg)
{
	fprintf(logFile,"%s\n", msg);
	fprintf(logFile,"+----------+----------+------------------+------------------+--------+--------+-----+------------------+------------------+");
	fprintf(logFile,"------------------+------------------+------------------+--------+\n");

	fprintf(logFile,"|          |          |                  |                  | ca     | cd     |     |                  |                  |");
	fprintf(logFile,"                  |                  |                  | scen-  |\n");

	fprintf(logFile,"| zbadgeid |   crewid | start time       | end time         | reg    | reg    | pos | scheduled_on     | scheduled_off    |");
	fprintf(logFile," actual_on        | actual_off       | currdate         | arioid |\n");

	fprintf(logFile,"+----------+----------+------------------+------------------+--------+--------+-----+------------------+------------------+");
	fprintf(logFile,"------------------+------------------+------------------+--------+\n");
}

static void
showCsCrewDataFooter(void)
{
	fprintf(logFile,"+----------+----------+------------------+------------------+--------+--------+-----+------------------+------------------+");
	fprintf(logFile,"------------------+------------------+------------------+--------+\n");
	fprintf(logFile,"\n");
}

static void
showCsCrewData(CS_CrewData *cdPtr)
{
	char dbuf1[32];
	char dbuf2[32];
	char dbuf3[32];
	char dbuf4[32];
	char dbuf5[32];
	char dbuf6[32];
	char dbuf7[32];
	char tbuf[32];

	BINTREENODE *tmp;
	CrewID *cidPtr, cidBuf;
	cidBuf.crewid = cdPtr->crewid;
	tmp = TreeSearch(crewidRoot, &cidBuf, crewidCmp);
	if(tmp) {
		cidPtr = getTreeObject(tmp);
		strcpy(tbuf, cidPtr->employeenumber);
	}
	else
		sprintf(tbuf,"%d", cdPtr->crewid);

	fprintf(logFile,"| %-8s | %8d | %s | %s | %-6s | %-6s | %3d | %s | %s | %s | %s | %s | %6d |\n",
    		tbuf,
		cdPtr->crewid,
		dt_DateTimeToDateTimeString(cdPtr->starttime, dbuf1, "%Y/%m/%d %H:%M"),
		dt_DateTimeToDateTimeString(cdPtr->endtime, dbuf2, "%Y/%m/%d %H:%M"),
    		cdPtr->ca_registration,
    		cdPtr->cd_registration,
    		cdPtr->position,
    		(cdPtr->scheduled_on) ? dt_DateTimeToDateTimeString(cdPtr->scheduled_on, dbuf3, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
    		(cdPtr->scheduled_off) ? dt_DateTimeToDateTimeString(cdPtr->scheduled_off, dbuf4, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
    		(cdPtr->actual_on) ? dt_DateTimeToDateTimeString(cdPtr->actual_on, dbuf5, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
    		(cdPtr->actual_off) ? dt_DateTimeToDateTimeString(cdPtr->actual_off, dbuf6, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
    		(cdPtr->currdate) ? dt_DateTimeToDateTimeString(cdPtr->currdate, dbuf7, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
    		cdPtr->scenarioid);
	return;

}

static void
showSS_CrewDataHeader(char *msg)
{
	fprintf(logFile,"%s\n", msg);
	fprintf(logFile,"+----------+-----------+-----------------+----------------+-------------+-------------+------+------+-------+------------------------+--------+");
	fprintf(logFile,"------+------------------------------+-------------------------------------+\n");

	fprintf(logFile,"|          |           |                 |                |             |             |      |  emp |       |                        |        |");
	fprintf(logFile," zacc |                              |                                     |\n");


	fprintf(logFile,"|          |           |                 |                |             |             | lemp | info | lpost |                        | zshift |");
	fprintf(logFile," code |                              |                                     |\n");


	fprintf(logFile,"| zbadgeid | zdeptdesc | dtdate          | zlname         | zfname      | zmname      |   id |   id |    id | zpostdesc              | desc   |");
	fprintf(logFile," id   | zacccodedesc                 | znote                               |\n");


	fprintf(logFile,"+----------+-----------+-----------------+----------------+-------------+-------------+------+------+-------+------------------------+--------+");
	fprintf(logFile,"------+------------------------------+-------------------------------------+\n");
}

static void
showSS_CrewDataFooter(void)
{
	fprintf(logFile,"+----------+-----------+------------+----------------+-------------+-------------+------+------+-------+------------------------+--------+");
	fprintf(logFile,"------+------------------------------+-------------------------------------+\n");
	fprintf(logFile,"\n\n\n\n");
}

static void
showSS_CrewData(SS_CrewData *ssCdPtr)
{
	char dbuf1[32];

	fprintf(logFile,"| %-8s | %-9s | %-16s | %-14s | %-11s | %-11s | %4d | %4d | %5d | %-22s | %-6s | %-4s | %-28s | %-35s | %5d | %5d |\n",
		ssCdPtr->zbadgeid,
		ssCdPtr->zdeptdesc,
		dt_DateTimeToDateTimeString(ssCdPtr->dtdate, dbuf1, "%Y/%m/%d %H:%M"),
		ssCdPtr->zlname,
		ssCdPtr->zfname,
		(ssCdPtr->zmname) ? ssCdPtr->zmname : "",
		ssCdPtr->lempid,
		ssCdPtr->lempinfoid,
		ssCdPtr->lpostid,
		ssCdPtr->zpostdesc,
		ssCdPtr->zshiftdesc,
		ssCdPtr->zacccodeid,
		(ssCdPtr->zacccodedesc) ? ssCdPtr->zacccodedesc : "",
		(ssCdPtr->znote) ? ssCdPtr->znote : "",
		(ssCdPtr->baseAirportID) ? ssCdPtr->baseAirportID : 0,
	    (ssCdPtr->crewID)? ssCdPtr->crewID : 0 );
}

static void
showCombined_CrewDataHeader(char *caption)
{
	fprintf(logFile,"%s\n", caption);
	fprintf(logFile,"  +----------+----------------+-----------+------------------------+--------+------------+--------+-----+------------------+------------------+");
	fprintf(logFile,"------+------------------------------+-------------------------------------+\n");

	fprintf(logFile,"  |          |                |           |                        |        |            |        |     |                  |                  |");
	fprintf(logFile," zacc |                              |                                     |\n");

	fprintf(logFile,"  |          |                |           |                        | zshift |            |        |     |                  |                  |");
	fprintf(logFile," code |                              |                                     |\n");


	fprintf(logFile,"  | zbadgeid | zlname         | zdeptdesc | zpostdesc              | desc   | dtdate     | reg    | pos | starttime        | endtime          |");
	fprintf(logFile," id   | zacccodedesc                 | znote                               |\n");

	fprintf(logFile,"  +----------+----------------+-----------+------------------------+--------+------------+--------+-----+------------------+------------------+");
	fprintf(logFile,"------+------------------------------+-------------------------------------+\n");

}

static void
showCombined_CrewDataPreCrewHeader(char *caption)
{
	fprintf(logFile,"%s\n", caption);
	fprintf(logFile,"  +----------+----------------+-----------+------------------------+--------+------------+--------+-----+------------------+------------------+");
	fprintf(logFile,"------+------------+----------+\n");

	fprintf(logFile,"  |          |                |           |                        |        |            |        |     |                  |                  |");
	fprintf(logFile," zacc |            |          |\n");

	fprintf(logFile,"  |          |                |           |                        | zshift |            |        |     |                  |                  |");
	fprintf(logFile," code |            |          |\n");


	fprintf(logFile,"  | zbadgeid | zlname         | zdeptdesc | zpostdesc              | desc   | dtdate     | reg    | pos | starttime        | endtime          |");
	fprintf(logFile," id   | startEarly | stayLate |\n");

	fprintf(logFile,"  +----------+----------------+-----------+------------------------+--------+------------+--------+-----+------------------+------------------+");
	fprintf(logFile,"------+------------+----------+\n");

}

static void
showCombined_CrewDataFooter(char *msg)
{
	fprintf(logFile,"  +----------+----------------+-----------+------------------------+--------+------------+--------+-----+------------------+------------------+");
	fprintf(logFile,"------+------------------------------+-------------------------------------+\n");
	if(msg)
		fprintf(logFile,"%s\n", msg);
	fprintf(logFile,"\n\n\n\n");
}


static void
showCombined_CrewData(SS_CrewData *ssCdPtr)
{
	char dbuf1[256];
	char dbuf2[32];
	BINTREENODE *tmp;
	CrewData *bwPtr;
	int firstOne = 1;

	if(ssCdPtr->bwWorkRoot) {
		tmp = Minimum(ssCdPtr->bwWorkRoot);
		bwPtr = (CrewData *) getTreeObject(tmp);
		if(bwPtr->position == 1 && strcmp(ssCdPtr->zshiftdesc,"SIC") == 0)
			fprintf(logFile,"! ");
		else
			fprintf(logFile,"  ");
	}
	else {
		strcpy(dbuf1, returnUpper(ssCdPtr->zpostdesc));
		if(cantFly(dbuf1))
			fprintf(logFile,"  ");
		else {
			if(!(strcasecmp(ssCdPtr->zacccodeid,"AOT") == 0 || strcasecmp(ssCdPtr->zacccodeid,"DO") == 0))
				fprintf(logFile,"* ");
			else
				fprintf(logFile,"  ");
		}
	}

	fprintf(logFile,"| %-8s | %-14s | %-9s | %-22s | %-6s | %s |",
		ssCdPtr->zbadgeid,
		ssCdPtr->zlname,
		ssCdPtr->zdeptdesc,
		ssCdPtr->zpostdesc,
		ssCdPtr->zshiftdesc,
		dt_DateTimeToDateTimeString(ssCdPtr->dtdate, dbuf1, "%Y/%m/%d"));

	for(tmp = Minimum(ssCdPtr->bwWorkRoot); tmp; tmp = Successor(tmp)) {
		bwPtr = (CrewData *) getTreeObject(tmp);
		if(firstOne) {
			firstOne = 0;
			fprintf(logFile," %-6s | %3d | %s | %s |",
			bwPtr->registration,
			bwPtr->position,
			//(gmtDayInSSdayRange(ssCdPtr->dtdate, bwPtr->starttime, ssCdPtr->dtdate)) ?
			(bwPtr->starttime)?
			    dt_DateTimeToDateTimeString(bwPtr->starttime, dbuf1, "%Y/%m/%d %H:%M") : "                ",
			//(gmtDayInSSdayRange(ssCdPtr->dtdate, bwPtr->endtime, ssCdPtr->dtdate)) ?
			(bwPtr->starttime)?
			    dt_DateTimeToDateTimeString(bwPtr->endtime, dbuf2, "%Y/%m/%d %H:%M") : "                ");
			fprintf(logFile," %-4s | %-28s | %-35s |\n", ssCdPtr->zacccodeid, ssCdPtr->zacccodedesc, (ssCdPtr->znote) ? ssCdPtr->znote : "");
		}
		else {
			fprintf(logFile,"%c |          |                |           |                        |        |            | %-6s | %3d | %s | %s |",
                        (bwPtr->position == 1 && strcmp(ssCdPtr->zshiftdesc,"SIC") == 0) ? '!' : ' ',
			bwPtr->registration,
			bwPtr->position,
			(gmtDayInSSdayRange(ssCdPtr->dtdate, bwPtr->starttime, ssCdPtr->dtdate)) ?
			    dt_DateTimeToDateTimeString(bwPtr->starttime, dbuf1, "%Y/%m/%d %H:%M") : "                ",
			(gmtDayInSSdayRange(ssCdPtr->dtdate, bwPtr->endtime, ssCdPtr->dtdate)) ?
			    dt_DateTimeToDateTimeString(bwPtr->endtime, dbuf2, "%Y/%m/%d %H:%M") : "                ");
			fprintf(logFile,"      |                              |                                     |\n");
		}
	}
	if(firstOne) {
		fprintf(logFile,"        |     |                  |                  |");
		fprintf(logFile," %-4s | %-28s | %-35s |\n", ssCdPtr->zacccodeid, (ssCdPtr->zacccodedesc) ? ssCdPtr->zacccodedesc : "",
			(ssCdPtr->znote) ? ssCdPtr->znote : "");
	}
}

static void
showCombined_CrewDataPreCrew_OnOff(PRE_Crew *preCrewPtr)
{
	char tbuf1[32], tbuf2[32], tbuf3[32], tbuf4[32], tbuf5[32], tbuf6[32], tbuf7[32];
	fprintf(logFile,"| %s | %4d | %s | %s | %s | %s | %4d  | %s | %2d | %5d | %5d | %4d | %s | %s |  \n",
    preCrewPtr->employeenumber,
	preCrewPtr->crewID,
	(preCrewPtr->on1) ? dt_DateTimeToDateTimeString(preCrewPtr->on1, tbuf1, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
	(preCrewPtr->off1) ? dt_DateTimeToDateTimeString(preCrewPtr->off1, tbuf2, "%Y/%m/%d %H:%M"): "0000/00/00 00:00",
	(preCrewPtr->on2) ? dt_DateTimeToDateTimeString(preCrewPtr->on2, tbuf3, "%Y/%m/%d %H:%M") : "0000/00/00 00:00",
	(preCrewPtr->off2) ? dt_DateTimeToDateTimeString(preCrewPtr->off2, tbuf4, "%Y/%m/%d %H:%M"): "0000/00/00 00:00",	
	preCrewPtr->availAirportID,
	(preCrewPtr->cscd_availDT) ? dt_DateTimeToDateTimeString(preCrewPtr->cscd_availDT, tbuf5, "%Y/%m/%d %H:%M"): "0000/00/00 00:00",
    preCrewPtr->cscd_canStartLater,
	preCrewPtr->dutyTime,
	preCrewPtr->blockTime,
    preCrewPtr->startLoc,
    (preCrewPtr->tourStartTm) ? dt_DateTimeToDateTimeString(preCrewPtr->tourStartTm, tbuf6, "%Y/%m/%d %H:%M"): "0000/00/00 00:00",
    (preCrewPtr->tourEndTm) ? dt_DateTimeToDateTimeString(preCrewPtr->tourEndTm, tbuf7, "%Y/%m/%d %H:%M"): "0000/00/00 00:00"); 
	if (preCrewPtr->lastActivityLeg){
		fprintf(logFile,"|lastActivityLeg:  |");
		showRawAircraftData(preCrewPtr->lastActivityLeg);
	}
	if (preCrewPtr->firstFlightOfTour){
		fprintf(logFile,"|firstFlightOfTour:|");
		showRawAircraftData(preCrewPtr->firstFlightOfTour);
	}
	if (preCrewPtr->lastFlightOfTour){
		fprintf(logFile,"|lastFlightOfTour: |");
		showRawAircraftData(preCrewPtr->lastFlightOfTour);
	}
	if (preCrewPtr->lastCsTravelLeg){
		fprintf(logFile,"|lastComTravel: |");
		showTravelLeg(preCrewPtr->lastCsTravelLeg);
	}		
}


static void
showCombined_CrewDataPreCrew(PRE_Crew *preCrewPtr)
{
	char dbuf1[256];
	char dbuf2[32];
	char tbuf1[32];
	char tbuf2[32];
	char tbuf3[32];
	char tbuf4[32];
	char tbuf5[32];
	char tbuf6[32];
	char tbuf7[32];
	char tbuf8[32];
	BINTREENODE *tmp, *tmp1, *tmpSS, *tmpBW, *tmpCS;
	int assignedToAC[20];
	CrewData *bwPtr;
	CS_CrewData *crwPtr;
	RawAircraftData *radPtr, radBuf;
	int firstOne, idx;
	int didStartEarlyStayLate = 0;
	SS_CrewData *ssCdPtr;
	LookupRet lkRet;
	CrewID *cidPtr, cidBuf;
	int hours, minutes;
	Time cscd_dutyTime, cscd_blockTime;

	for(tmp1 = Minimum(preCrewPtr->ssCrewDataRoot); tmp1; tmp1 = Successor(tmp1)) {
		firstOne = 1;
		ssCdPtr = (SS_CrewData *) getTreeObject(tmp1);

		if(ssCdPtr->bwWorkRoot) {
			tmp = Minimum(ssCdPtr->bwWorkRoot);
			bwPtr = (CrewData *) getTreeObject(tmp);
			if(bwPtr->position == 1 && strcmp(ssCdPtr->zshiftdesc,"SIC") == 0)
				fprintf(logFile,"! ");
			else
				fprintf(logFile,"  ");
		}
		else {
			strcpy(dbuf1, returnUpper(ssCdPtr->zpostdesc));
			if(cantFly(dbuf1))
				fprintf(logFile,"  ");
			else {
				if(!(strcasecmp(ssCdPtr->zacccodeid,"AOT") == 0 || strcasecmp(ssCdPtr->zacccodeid,"DO") == 0))
					fprintf(logFile,"* ");
				else
					fprintf(logFile,"  ");
			}
		}
	
		fprintf(logFile,"| %-8s | %-14s | %-9s | %-22s | %-6s |%c%s",
			ssCdPtr->zbadgeid,
			ssCdPtr->zlname,
			ssCdPtr->zdeptdesc,
			ssCdPtr->zpostdesc,
			ssCdPtr->zshiftdesc,
			//(ssCdPtr->dtdate == (preCrewPtr->tourStartTm & DateOnly)) ? '+' : ' ',
			(OnSSday(preCrewPtr->tourStartTm,ssCdPtr->dtdate)) ? '+' : ' ',
			dt_DateTimeToDateTimeString(ssCdPtr->dtdate, dbuf1, "%Y/%m/%d"));
		//fprintf(logFile,"%c|", (ssCdPtr->dtdate == (preCrewPtr->tourEndTm & DateOnly)) ? '-' : ' ');
		//fprintf(logFile,"%c|", (OnSSday(preCrewPtr->tourEndTm,ssCdPtr->dtdate)) ? '-' : ' ');
		//fprintf(logFile,"%c|", (SSdayInRange(preCrewPtr->tourEndTm, ssCdPtr->dtdate,preCrewPtr->tourEndTm) && // experimental debug
		//	(dt_addToDateTime(Hours, - 14, preCrewPtr->tourEndTm) & DateOnly) == ssCdPtr->dtdate) ? '-' : ' '); // experimental debug
		fprintf(logFile,"%c|", ((dt_addToDateTime(Hours, -24, preCrewPtr->tourEndTm)) == ssCdPtr->dtdate) ? '-' : ' '); // experimental debug
	
		for(tmp = Minimum(ssCdPtr->bwWorkRoot); tmp; tmp = Successor(tmp)) {
			bwPtr = (CrewData *) getTreeObject(tmp);
			if(firstOne) {
				firstOne = 0;
				fprintf(logFile," %-6s | %3d | %s | %s |",
				bwPtr->registration,
				bwPtr->position,
				(gmtDayInSSdayRange(ssCdPtr->dtdate, bwPtr->starttime, ssCdPtr->dtdate)) ?
					dt_DateTimeToDateTimeString( bwPtr->starttime, dbuf1, "%Y/%m/%d %H:%M") : "                ",
				(gmtDayInSSdayRange(ssCdPtr->dtdate, bwPtr->endtime, ssCdPtr->dtdate)) ?
					dt_DateTimeToDateTimeString( bwPtr->endtime, dbuf2, "%Y/%m/%d %H:%M") : "                ");
			if(!didStartEarlyStayLate) {
					fprintf(logFile," %-4s | %-10.2f | %8.2f |\n",
					    ssCdPtr->zacccodeid, preCrewPtr->startEarly, preCrewPtr->stayLate);
					didStartEarlyStayLate = 1;
				}
				else
					fprintf(logFile," %-4s |            |          |\n", ssCdPtr->zacccodeid);
			}
			else {
				fprintf(logFile,"%c |          |                |           |                        |        |            | %-6s | %3d | %s | %s |",
	                        (bwPtr->position == 1 && strcmp(ssCdPtr->zshiftdesc,"SIC") == 0) ? '!' : ' ',
				bwPtr->registration,
				bwPtr->position,
				(gmtDayInSSdayRange(ssCdPtr->dtdate, bwPtr->starttime, ssCdPtr->dtdate)) ?
					dt_DateTimeToDateTimeString( bwPtr->starttime, dbuf1, "%Y/%m/%d %H:%M") : "                ",
				(gmtDayInSSdayRange(ssCdPtr->dtdate, bwPtr->endtime, ssCdPtr->dtdate)) ?
					dt_DateTimeToDateTimeString( bwPtr->endtime, dbuf2, "%Y/%m/%d %H:%M") : "                ");
				fprintf(logFile,"      |            |          |\n");
			}
		}
		if(firstOne) {
			fprintf(logFile,"        |     |                  |                  |");
			if(!didStartEarlyStayLate) {
				fprintf(logFile," %-4s | %-10.2f | %8.2f |\n", ssCdPtr->zacccodeid, preCrewPtr->startEarly, preCrewPtr->stayLate);
				didStartEarlyStayLate = 1;
			}
			else
				fprintf(logFile," %-4s |            |          |\n", ssCdPtr->zacccodeid);
		}
	}
	fprintf(logFile,"  +----------+----------------+-----------+------------------------+--------+------------+--------+-----+------------------+------------------+");
	fprintf(logFile,"------+------------+----------+\n");

	cidBuf.crewid = preCrewPtr->crewID;
	tmp = TreeSearch(crewidRoot, &cidBuf, crewidCmp);
	if(tmp) {
		cidPtr = getTreeObject(tmp);
		sprintf(dbuf1, "CS Crew Data for %s, base airport: %s", cidPtr->employeenumber, cidPtr->baseicao);
	}
	else
		strcpy(dbuf1,"CS Crew Data");

	showCsCrewDataHeader(dbuf1);
	for(tmp = Minimum(preCrewPtr->csCrewDataRoot); tmp; tmp = Successor(tmp)) {
		crwPtr = (CS_CrewData *) getTreeObject(tmp);
		showCsCrewData(crwPtr);
	}
	showCsCrewDataFooter();

	for(idx = 0; idx < 20; ++idx)
		assignedToAC[idx] = 0;
	idx = 0;
	for(tmpSS = Minimum(preCrewPtr->ssCrewDataRoot); tmpSS; tmpSS = Successor(tmpSS)) {
		ssCdPtr = (SS_CrewData *) getTreeObject(tmpSS);
		for(tmpBW = Minimum(ssCdPtr->bwWorkRoot); tmpBW; tmpBW = Successor(tmpBW)) {
			bwPtr = (CrewData *) getTreeObject(tmpBW);
			if(bwPtr->aircraftid) {
				if(checkAircraftList(assignedToAC, &idx, bwPtr->aircraftid, 20)) {
					showAcListEntry(bwPtr->aircraftid);
					showRawAircraftDataHeader();
					memset(&radBuf, '\0', sizeof(radBuf));
					radBuf.aircraftid = bwPtr->aircraftid;
					lkRet = Lookup(rawAircraftRoot, &radBuf, rawAircraftCompare, &tmp);
					switch(lkRet) {
					case EmptyTree:
					case GreaterThanLastItem:
						logMsg(logFile,"%s Line %d: aircraft %d not found. ret=%d\n", __FILE__,__LINE__,
							bwPtr->aircraftid, lkRet);
						break;
					case ExactMatchFound: //aircraft record
					case NotFoundReturnedNextItem:
						for(; tmp; tmp = Successor(tmp)) {
							radPtr = (RawAircraftData *) getTreeObject(tmp);
							if(radPtr->aircraftid != bwPtr->aircraftid)
								break;
							showRawAircraftData(radPtr);	
						}
						showRawAircraftDataFooter();
						break;
					}
				}
			}
		}
	}
	for(tmpCS = Minimum(preCrewPtr->csCrewDataRoot); tmpCS; tmpCS = Successor(tmpCS)) {
		crwPtr = (CS_CrewData *) getTreeObject(tmpCS);
		if(crwPtr->ca_aircraftid) {
			if(checkAircraftList(assignedToAC, &idx, crwPtr->ca_aircraftid, 20)) {
				showAcListEntry(crwPtr->ca_aircraftid);
				showRawAircraftDataHeader();
				memset(&radBuf, '\0', sizeof(radBuf));
				radBuf.aircraftid = crwPtr->ca_aircraftid;
				lkRet = Lookup(rawAircraftRoot, &radBuf, rawAircraftCompare, &tmp);
				switch(lkRet) {
				case EmptyTree:
				case GreaterThanLastItem:
					logMsg(logFile,"%s Line %d: aircraft %d not found. ret=%d\n", __FILE__,__LINE__,
						crwPtr->ca_aircraftid, lkRet);
					break;
				case ExactMatchFound: //aircraft record
				case NotFoundReturnedNextItem:
					for(; tmp; tmp = Successor(tmp)) {
						radPtr = (RawAircraftData *) getTreeObject(tmp);
						if(radPtr->aircraftid != crwPtr->ca_aircraftid)
							break;
						showRawAircraftData(radPtr);	
					}
					showRawAircraftDataFooter();
					break;
				}
			}
		}
		if(crwPtr->cd_aircraftid) {
			if(checkAircraftList(assignedToAC, &idx, crwPtr->cd_aircraftid, 20)) {
				showAcListEntry(crwPtr->cd_aircraftid);
				showRawAircraftDataHeader();
				memset(&radBuf, '\0', sizeof(radBuf));
				radBuf.aircraftid = crwPtr->cd_aircraftid;
				lkRet = Lookup(rawAircraftRoot, &radBuf, rawAircraftCompare, &tmp);
				switch(lkRet) {
				case EmptyTree:
				case GreaterThanLastItem:
					logMsg(logFile,"%s Line %d: aircraft %d not found. ret=%d\n", __FILE__,__LINE__,
						crwPtr->cd_aircraftid, lkRet);
					break;
				case ExactMatchFound: // aircraft record
				case NotFoundReturnedNextItem:
					for(; tmp; tmp = Successor(tmp)) {
						radPtr = (RawAircraftData *) getTreeObject(tmp);
						if(radPtr->aircraftid != crwPtr->cd_aircraftid)
							break;
						showRawAircraftData(radPtr);
					}
					showRawAircraftDataFooter();
					break;
				}
			}
		}
	}
	if(idx == 0) {
		showRawAircraftDataHeader();
		showRawAircraftDataFooter();
	}
	
	hours = preCrewPtr->dutyTime / 60;
	minutes = preCrewPtr->dutyTime % 60;
	cscd_dutyTime = dt_HMSMtoTime(hours, minutes, 0, 0);

	hours = preCrewPtr->blockTime / 60;
	minutes = preCrewPtr->blockTime % 60;
	cscd_blockTime = dt_HMSMtoTime(hours, minutes, 0, 0);

	tbuf1[0] = '\0';
	if(preCrewPtr->startLoc) {
		char *p;
		if((p = aptIdToIcao(preCrewPtr->startLoc)))
			strcpy(tbuf1,p);
	}

	tbuf2[0] = '\0';
	if(preCrewPtr->endLoc) {
		char *p;
		if((p = aptIdToIcao(preCrewPtr->endLoc)))
			strcpy(tbuf2,p);
	}

	tbuf3[0] = '\0';
	if(preCrewPtr->availAirportID) {
		char *p;
		if((p = aptIdToIcao(preCrewPtr->availAirportID)))
			strcpy(tbuf3,p);
	}

	fprintf(logFile,"+-----+-------+-------+-------+------------------+-------+-------+-------+------------------+------------------+\n");
	fprintf(logFile,"|     |       |       |       |                  | can   |       |       | tour             | tour             +\n");
	fprintf(logFile,"| log | start | end   | avail |                  | start | duty  | block | start            | end              +\n");
	fprintf(logFile,"| ic  | loc   | loc   | apt   | availDT          | later | time  | time  | time             | time             +\n");
	fprintf(logFile,"+-----+-------+-------+-------+------------------+-------+-------+-------+------------------+------------------+\n");
	fprintf(logFile,"| %3d | %5s | %5s | %5s | %s | %5d | %s | %s | %s | %s |\n",
	    preCrewPtr->newLogic,
	    tbuf1, tbuf2, tbuf3,
	    (preCrewPtr->cscd_availDT) ? dt_DateTimeToDateTimeString(preCrewPtr->cscd_availDT,tbuf6,"%Y/%m/%d %H:%M") : "                ",
	    preCrewPtr->cscd_canStartLater,
	    dt_TimeToTimeString(cscd_dutyTime, tbuf4,"%H:%M"),
	    dt_TimeToTimeString(cscd_blockTime, tbuf5,"%H:%M"),
	    (preCrewPtr->tourStartTm) ? dt_DateTimeToDateTimeString(preCrewPtr->tourStartTm,tbuf7,"%Y/%m/%d %H:%M") : "                ",
	    (preCrewPtr->tourEndTm) ? dt_DateTimeToDateTimeString(preCrewPtr->tourEndTm,tbuf8,"%Y/%m/%d %H:%M") : "                ");
	fprintf(logFile,"+-----+-------+-------+-------+------------------+-------+-------+-------+------------------+------------------+\n");

	fprintf(logFile,"\n\n\n\n");

}

/* 
 return 0 if you have already done this one
 return 1 if you haven't already done this one
*/
static int
checkAircraftList(int *list, int *count, int acid, int max)
{
	int x;
	if(*count >= max)
		return(0);
	for(x = 0; x < *count; ++x) {
		if(*(list + x) == acid)
			return(0);
	}
	*(list + *count) = acid;
	*count = *count + 1;
	return(1);
}

static char *
returnUpper(char *s)
{
	static char tbuf[1024];
	int len = (int) strlen(s);
        char *t = tbuf;

	if(len >= 1024) {
		logMsg(logFile,"%s Line %d: String too big in returnUpper()\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	strcpy(tbuf,s);
        while(*t) {
                if(*t >= 'a' && *t <= 'z')
                        *t -= ('a' - 'A');
                ++t;
        }
        return(tbuf);
}

static int
getAcTypeID(char *arg)
{
	if(strcasecmp("Bravo", arg) == 0)
		return(5);
	if(strcasecmp("CJ1", arg) == 0)
		return(6);
	if(strcasecmp("CJ3", arg) == 0)
		return(54);
	if(strcasecmp("Excel", arg) == 0)
		return(11);
	if(strcasecmp("Sovereign", arg) == 0)
		return(52);
	return(0);
}

static int
analysePositions(PRE_Crew *pre_crewPtr)
{
	char tbuf[1024];
	BINTREENODE *tmp;
	SS_CrewData *ssCrewPtr;

	int pic_count = 0, sic_count = 0, other_count = 0;
	int ret;

	sprintf(tbuf,"Analysing positions for %s", pre_crewPtr->employeenumber);
	showCombined_CrewDataHeader(tbuf);
	for(tmp = Minimum(pre_crewPtr->ssCrewDataRoot); tmp; tmp = Successor(tmp)) {
		ssCrewPtr = (SS_CrewData *) getTreeObject(tmp);
		if(strcasecmp(ssCrewPtr->zshiftdesc, "PIC") == 0)
			++pic_count;
		else if(strcasecmp(ssCrewPtr->zshiftdesc, "SIC") == 0)
			++sic_count;
		else
			++other_count;
		showCombined_CrewData(ssCrewPtr);
	}
	if(pic_count >= sic_count)
		ret = 1;
	else
		ret = 2;
	if(other_count) {
		sprintf(tbuf,"Setting position to %d (%s). A position other than \"PIC\" or \"SIC\" was found %d time(s).",
			ret,
			(ret == 1) ? "PIC" : "SIC",
			other_count);
	}
	else
		sprintf(tbuf,"setting position to %d.", ret);
	showCombined_CrewDataFooter(tbuf);
	return(ret);
}

typedef enum { AcTypeBravo = 0, AcTypeCJ1, AcTypeCJ3, AcTypeExcel, AcTypeSovereign, AcTypeOther, AcTypeCount } AcTypeEnum;
static int
analyseAircraftType(PRE_Crew *pre_crewPtr)
{
	char tbuf[1024];
	BINTREENODE *tmp;
	SS_CrewData *ssCrewPtr;

	int most, acType;
	AcTypeEnum idx;
	AcTypeEnum thisType;
	int counts[] = { 0,0,0,0,0,0 };
	int returnValues[] = { 5, 6, 54, 11, 52, 0 };
	char *acStr[] = { "Bravo", "CJ1", "CJ3", "Excel", "Sovereign", "Other" };


	sprintf(tbuf,"Analysing aircraft types for %s", pre_crewPtr->employeenumber);
	showCombined_CrewDataHeader(tbuf);
	for(tmp = Minimum(pre_crewPtr->ssCrewDataRoot); tmp; tmp = Successor(tmp)) {
		ssCrewPtr = (SS_CrewData *) getTreeObject(tmp);

		acType = getAcTypeID(ssCrewPtr->zdeptdesc);
		switch(acType) {
		case 5: // Bravo
			counts[AcTypeBravo]++;
			break;
		case 6: // CJ1
			counts[AcTypeCJ1]++;
			break;
		case 54: // CJ3
			counts[AcTypeCJ3]++;
			break;
		case 11: // Excel
			counts[AcTypeExcel]++;
			break;
		case 52: // Sovereign
			counts[AcTypeSovereign]++;
			break;
		default:
			counts[AcTypeOther]++;
			break;
		}
		showCombined_CrewData(ssCrewPtr);
	}
	most = 0;
	for(thisType = AcTypeBravo, idx = AcTypeBravo; idx < AcTypeCount; ++idx) {
		if(counts[idx] > most) {
			most = counts[idx];
			thisType = idx;
		}
	}
	if(counts[AcTypeOther] > 0)
		sprintf(tbuf,"Found unknown aircraft type! Setting aircraft type to %s.", acStr[thisType]);
	else
		sprintf(tbuf,"Setting aircraft type to %s.", acStr[thisType]);

	showCombined_CrewDataFooter(tbuf);
	return(returnValues[thisType]);
}

// str should be mapped to upper case already
static int
cantFly(char *str)
{
	static char *notFlying[] = { "TRAIN", "FLIGHT", "REC NOT IN SS", (char *) 0 };

	int i = 0;
	
	while(notFlying[i]) {
		if(strstr(str,notFlying[i]))
			return(1);
		++i;
		
	}
	return(0);
}

/********************************************************************************
*	Function   readLegData()
*	Purpose:  read in managed legs and logpageleg records where the
*	scheduled out is in the planning window
********************************************************************************/
static int
readLegData(MY_CONNECTION *myconn)
{

	extern char *legSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	BINTREENODE *tmp, *oldTree;
	char tbuf1[32], tbuf2[32];

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	LEG_DATA *legPtr;
	Leg *lPtr;
	int a, skip = 0, tempACID = 0; // 01/05/09 ANG

	if(!myDoQuery(myconn, legSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readLegData(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readLegData(): 0 rows returned.\n");
		//return(0);
	}
#ifdef DEBUGGING
	logMsg(logFile,"raw query results for legSQL:\n");
	if (verbose) displayResults(res, cols);
	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
#endif // DEBUGGING

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;

		//START - Ignore all legs related to MAC - MAC - 01/05/09 ANG
		if(optParam.ignoreMac == 1){
			skip = 0;
			tempACID = (row[LD_aircraftid]) ? atoi(row[LD_aircraftid]) : 0;
			if(tempACID > 0){
				//search through M-aircraft list
				for(a = 0; a < numMacInfo; a++){
					if(atoi(row[LD_aircraftid]) == macInfoList[a].aircraftID){
						//nbSkip++;
						skip = 1;
						//if (verbose) {
						//	fprintf(logFile, "Skipping OS record %d for aircraftID %d.\n", atoi(row[whereRec_id]), macInfoList[a].aircraftID);
						//}
						break;
					}
				}
				if (skip == 1)
					continue;
			}
		}
		//END - MAC - 01/05/09 ANG

		if(row[LD_demandid]) {
			int tmpDmdID;
			BINTREENODE *tmp2;
			Demand dmdBuf;
			if(!(tmpDmdID = atoi(row[LD_demandid]))) {
				logMsg(logFile,"%s Line %d, zero demandid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			dmdBuf.demandID = tmpDmdID;
			//skip if demand not in demand list
			tmp2 = TreeSearch(dmdXDemandIdRoot,&dmdBuf,demandIdCompare);
			if(! tmp2) {
				// didn't find this demandid.
				// cancelled or not in planning window
				logMsg(logFile,"%s Line %d, skipped %s %d, no demand %d\n", __FILE__,__LINE__, row[LD_rowtype], atoi(row[LD_rec_id]), tmpDmdID);
				continue;
				//***
			}
		}
		legPtr = textToLegData(row);
		if(! legPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readLegData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		oldTree = legRoot;

		if(!(legRoot = RBTreeInsert(legRoot, legPtr, legCmp))) {
			legRoot = oldTree;
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readLegData().\n",__FILE__,__LINE__);
			fprintf(logFile,"+-----------+---------+------------+--------+----------+----------+---------+---------+--------+------------------+------------------+\n");
			fprintf(logFile,"| rowtype   |   recid | aircraftid | reg    | demandid | outaptid | outicao | inaptid | inicao | rec_outtime      | rec_intime       |\n"); 
			fprintf(logFile,"+-----------+---------+------------+--------+----------+----------+---------+---------+--------+------------------+------------------+\n");
			fprintf(logFile,"| %-9s | %7d | %10d | %-6s | %8d | %8d | %-7s | %7d | %-6s | %s | %s |\n",
				legPtr->rowtype,
				legPtr->rec_id,
				legPtr->aircraftid,
				legPtr->registration,
				legPtr->demandid,
				legPtr->outaptid,
				legPtr->outicao,
				legPtr->inaptid,
				legPtr->inicao,
		    		(legPtr->rec_outtime) ? dt_DateTimeToDateTimeString(legPtr->rec_outtime,tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
		    		(legPtr->rec_intime) ? dt_DateTimeToDateTimeString(legPtr->rec_intime,tbuf2,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
			fprintf(logFile,"+-----------+---------+------------+--------+----------+----------+---------+---------+--------+------------------+------------------+\n");
			writeWarningData(myconn); 
			continue;  //Ignore the duplicate record.
			//exit(1);
		}
		++numLegs;
	}
	if(!(legList = (Leg *) calloc((size_t) numLegs + 1, sizeof(Leg)))) {
		logMsg(logFile,"%s Line %d, Out of Memory in readLegData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	lPtr = legList;
	for(tmp = Minimum(legRoot); tmp; tmp = Successor(tmp)) {
		legPtr = (LEG_DATA *) getTreeObject(tmp);
		lPtr->demandID = legPtr->demandid;
		lPtr->aircraftID = legPtr->aircraftid;
		lPtr->outAirportID = legPtr->outaptid;
		lPtr->inAirportID = legPtr->inaptid;
		lPtr->outFboID = legPtr->outfboid;
		lPtr->inFboID = legPtr->infboid;
		lPtr->schedOut =  DateTimeToTime_t(legPtr->rec_outtime);
		lPtr->schedIn =  DateTimeToTime_t(legPtr->rec_intime);
		lPtr->planeLocked = legPtr->manuallyassigned;
		++lPtr;
	}

	if(verbose) {
		logMsg(logFile,"\nlegs:\n");
		fprintf(logFile,"+-----------+---------+------------+--------+----------+----------+---------+---------+--------+------------------+------------------+--------+\n");
		fprintf(logFile,"| rowtype   |   recid | aircraftid | reg    | demandid | outaptid | outicao | inaptid | inicao | rec_outtime      | rec_intime       | Locked |\n"); 
		fprintf(logFile,"+-----------+---------+------------+--------+----------+----------+---------+---------+--------+------------------+------------------+--------+\n");
		for(tmp = Minimum(legRoot); tmp; tmp = Successor(tmp)) {
			legPtr = getTreeObject(tmp);
			fprintf(logFile,"| %-9s | %7d | %10d | %-6s | %8d | %8d | %-7s | %7d | %-6s | %s | %s | %6d |\n",
				legPtr->rowtype,
				legPtr->rec_id,
				legPtr->aircraftid,
				legPtr->registration,
				legPtr->demandid,
				legPtr->outaptid,
				legPtr->outicao,
				legPtr->inaptid,
				legPtr->inicao,
		    		(legPtr->rec_outtime) ? dt_DateTimeToDateTimeString(legPtr->rec_outtime,tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
		    		(legPtr->rec_intime) ? dt_DateTimeToDateTimeString(legPtr->rec_intime,tbuf2,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				legPtr->manuallyassigned);
		}
		fprintf(logFile,"+-----------+---------+------------+--------+----------+----------+---------+---------+--------+------------------+------------------+--------+\n\n\n\n");
		fflush(logFile);
	}

	// free mysql results
	mysql_free_result(res);
	return 0;

}



/********************************************************************************
*	Function   readFuelLegData() --Roger Did not use it so far 02/21/07
*	Purpose:  read in fuel stops from managed legs and logpageleg records where the
*	scheduled out is in the planning window
********************************************************************************/

/*
static int
readFuelLegData(MY_CONNECTION *myconn)
{

	extern char *fuelLegSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	char tbuf1[32], tbuf2[32];

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	LEG_DATA *legPtr;
	Leg *lPtr;
	int fuelLegIndex; 

	if(!myDoQuery(myconn, fuelLegSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readFuelLegData(): 0 rows returned.\n");
		return(0);
	}
#ifdef DEBUGGING
	logMsg(logFile,"raw query results for fuelLegSQL:\n");
	displayResults(res, cols);
	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
#endif // DEBUGGING

	if(!(fuelLegList = (Leg *) calloc((size_t) rowCount + 1, sizeof(Leg)))) {
		logMsg(logFile,"%s Line %d, Out of Memory in readFuelLegData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	lPtr = fuelLegList;
	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		legPtr = textToLegData(row);
		if(! legPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readFuelLegData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}		
		lPtr->demandID = legPtr->demandid;
		lPtr->aircraftID = legPtr->aircraftid;
		lPtr->outAirportID = legPtr->outaptid;
		lPtr->inAirportID = legPtr->inaptid;
		lPtr->schedOut =  DateTimeToTime_t(legPtr->rec_outtime);
		lPtr->schedIn =  DateTimeToTime_t(legPtr->rec_intime);
		++lPtr;
		++numFuelLegs;
	}


	if(verbose) {
		logMsg(logFile,"\nFuel Stop Legs:\n");
		fprintf(logFile,"+------------+----------+---------+---------+------------------+------------------+\n");
		fprintf(logFile,"| aircraftid | demandid |outaptid | inaptid | rec_outtime      | rec_intime       |\n"); 
		fprintf(logFile,"+------------+----------+---------+---------+------------------+------------------+\n");
		for(fuelLegIndex = 0; fuelLegIndex < numFuelLegs; fuelLegIndex++) {
			fprintf(logFile,"| %10d | %8d | %7d | %7d | %s | %s |\n",
				fuelLegList[fuelLegIndex].aircraftID,				
				fuelLegList[fuelLegIndex].demandID,
				fuelLegList[fuelLegIndex].outAirportID,
				fuelLegList[fuelLegIndex].inAirportID,
				(fuelLegList[fuelLegIndex].schedOut) ? dt_DateTimeToDateTimeString(dt_time_tToDateTime(fuelLegList[fuelLegIndex].schedOut),tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
		    	(fuelLegList[fuelLegIndex].schedIn) ? dt_DateTimeToDateTimeString(dt_time_tToDateTime(fuelLegList[fuelLegIndex].schedIn),tbuf2,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
	    }
		fprintf(logFile,"+------------+----------+---------+---------+------------------+------------------+\n\n\n\n");
		fflush(logFile);
	}

	// free mysql results
	mysql_free_result(res);
	return 0;

}
*/

/********************************************************************************
*	Function   readPairConstraints()
*	Purpose:  read in pair constraints
********************************************************************************/
static int
readPairConstraints(MY_CONNECTION *myconn)
{

	extern char *pairConstraintsSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	BINTREENODE *tmp;
	char tbuf1[32];
	int pcid, ppcid, errNbr;
	unsigned mask;
	DateTime tdate, pwStartDate, pwEndDate;

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	PairConstraint *pcPtr, *pcPtrA, pcBuf;
	PrePairConstraint ppcBuf;
	char tbufA[128];
	char tbufB[128];

	CrewPair *cpPtr; //01/22/09 ANG

	ppcBuf.name1 = tbufA;
	ppcBuf.name2 = tbufB;

	pwStartDate = dt_time_tToDateTime(optParam.windowStart);
	pwEndDate = dt_time_tToDateTime(optParam.windowEnd);

	if(!myDoQuery(myconn, pairConstraintsSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readPairConstraints(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readPairConstraints(): 0 rows returned.\n");
		return(0);
	}
#ifdef DEBUGGING
	logMsg(logFile,"raw query results for pairconstraintSQL:\n");
	if (verbose) displayResults(res, cols);
	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
#endif // DEBUGGING

	for(rows = 0, ppcid = 1; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		textToPairConstraint(row, &ppcBuf);
		pcBuf.crew1ID = ppcBuf.crewid1;
		pcBuf.crew2ID = ppcBuf.crewid2;
		pcBuf.priority = ppcBuf.priority;
		for(tdate = ppcBuf.startdate; tdate <= ppcBuf.enddate; tdate = AddDays(1, tdate)) {
			if(SSdayInRange(AddDays(-1,pwStartDate), tdate, AddDays(1,pwEndDate))) {
				pcBuf.startTm = DateTimeToTime_t(tdate);
				if(ppcBuf.crewid2) {
					pcBuf.categoryID = -1;
					if(!(tmp =  TreeSearch(pcCrewIdToCrewIdRoot,&pcBuf,pcCrewIdToCrewIdCmp))) {
						if(!(pcPtr = (PairConstraint *) calloc((size_t) 1, sizeof(PairConstraint)))) {
							logMsg(logFile,"%s Line %d, Out of Memory in readPairConstraints().\n", __FILE__,__LINE__);
							writeWarningData(myconn); exit(1);
						}
						memcpy(pcPtr, &pcBuf, sizeof(PairConstraint));

						//oldTree = pcCrewIdToCrewIdRoot;

						if(!(pcCrewIdToCrewIdRoot = RBTreeInsert(pcCrewIdToCrewIdRoot, pcPtr, pcCrewIdToCrewIdCmp))) {
							logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readPairConstraints().\n",__FILE__,__LINE__);
							displayPreCrewPair(&ppcBuf);
							writeWarningData(myconn); 
							//pcCrewIdToCrewIdRoot = oldTree;
							//continue;
							exit(1);
						}
						++numPairConstraints;
					}
					else {
						// exact same constraint already there but with a different priority.
						// use the highest priority (lowest number)
						pcPtr = (PairConstraint *) getTreeObject(tmp);
						pcPtr->priority = Min(pcPtr->priority, pcBuf.priority);
					}
				}
				else {
					for(mask = 1; mask != Bit_Stop; mask <<= 1) {
						if(testBit(ppcBuf.catIds,mask)) {
							pcBuf.categoryID = catBitToPost(mask);
							if(!(tmp = TreeSearch(pcCrewIdToCategoryRoot,&pcBuf,pcCrewIdToCategoryCmp))) {
								if(!(pcPtr = (PairConstraint *) calloc((size_t) 1, sizeof(PairConstraint)))) {
									logMsg(logFile,"%s Line %d, Out of Memory in readPairConstraints().\n", __FILE__,__LINE__);
									writeWarningData(myconn); exit(1);
								}
								memcpy(pcPtr, &pcBuf, sizeof(PairConstraint));


								if(!(pcCrewIdToCategoryRoot = RBTreeInsert(pcCrewIdToCategoryRoot, pcPtr, pcCrewIdToCategoryCmp))) {
									logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readPairConstraints().\n",__FILE__,__LINE__);
									displayPreCrewPair(&ppcBuf);
									writeWarningData(myconn); exit(1);
								}
								++numPairConstraints;
							}
							else {
								// exact same constraint already there but with a different priority.
								// use the highest priority (lowest number)
								pcPtr = (PairConstraint *) getTreeObject(tmp);
								pcPtr->priority = Min(pcPtr->priority, pcBuf.priority);
							}
						}
					}
				}
			}
		}
	}

	if(!(pairConstraintList = (PairConstraint *) calloc((size_t) numPairConstraints + 1, sizeof(PairConstraint)))) {
		logMsg(logFile,"%s Line %d, Out of Memory in readPairConstraints().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	pcPtr = pairConstraintList;
	pcid = 1;
	for(tmp = Minimum(pcCrewIdToCrewIdRoot); tmp; tmp = Successor(tmp)) {
		pcPtrA = (PairConstraint *) getTreeObject(tmp);
		memcpy(pcPtr, pcPtrA, sizeof(PairConstraint));
		pcPtr->pairConstraintID = pcid++;
		pcPtr++;
	}
	for(tmp = Minimum(pcCrewIdToCategoryRoot); tmp; tmp = Successor(tmp)) {
		pcPtrA = (PairConstraint *) getTreeObject(tmp);
		memcpy(pcPtr, pcPtrA, sizeof(PairConstraint));
		pcPtr->pairConstraintID = pcid++;
		pcPtr++;
	}

	if(verbose) {
		logMsg(logFile,"Pair Constraints:\n");
		fprintf(logFile,"+-----+---------+---------+------------+-----------------------------+----------+------------+\n");
		fprintf(logFile,"|  ID | crew1ID | crew2ID | categoryID | Category                    | priority | startTm    |\n");
		fprintf(logFile,"+-----+---------+---------+------------+-----------------------------+----------+------------+\n");
		for(pcPtr = pairConstraintList;pcPtr->pairConstraintID;++pcPtr) {
		fprintf(logFile,"| %3d | %7d | %7d | %10d | %27s | %8d | %s |\n",
			pcPtr->pairConstraintID,
			pcPtr->crew1ID,
			pcPtr->crew2ID,
			pcPtr->categoryID,
			postToPostNameText(pcPtr->categoryID,Post_Text),
			pcPtr->priority,
	    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
				asctime(gmtime(&(pcPtr->startTm))), NULL, &errNbr),tbuf1,"%Y/%m/%d"));
		}
		fprintf(logFile,"+-----+---------+---------+------------+-----------------------------+----------+------------+\n");
		fflush(logFile);
	}

	//START - Downgrade pair constraint with priority 1 to priority 2 if the crew member has flown some aircraft previously - 01/22/09 ANG
	if(optParam.downgrPairPriority1 == 1){
		cpPtr = (CrewPair *) calloc((size_t) 1, (size_t) sizeof(CrewPair));
		if(! cpPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readPairConstraints().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		for(pcPtr = pairConstraintList; pcPtr->pairConstraintID; ++pcPtr) {
			if(pcPtr->priority == 1){
				for(cpPtr = crewPairList; cpPtr->crewPairID; ++cpPtr){
					if(cpPtr->hasFlownFirst == 1){
						if( pcPtr->crew1ID == cpPtr->captainID ||
							pcPtr->crew2ID == cpPtr->captainID ||
							pcPtr->crew1ID == cpPtr->flightOffID ||
							pcPtr->crew2ID == cpPtr->flightOffID ){
							pcPtr->priority = 2;
							fprintf(logFile, "Pairing priority 1 for crew %d and %d is changed to priority 2 \n", pcPtr->crew1ID, pcPtr->crew2ID);
							break;
						}
					}
				}
			}
		}
	}
	//END - 01/22/09 ANG

	// free mysql results
	mysql_free_result(res);
	return 0;
}

static void
displayPreCrewPair(PrePairConstraint *ppcPtr)
{
	int didOne = 0;
	char tbuf1[32];
	char tbuf2[32];
	unsigned mask;

	fprintf(logFile,"+---------+---------+----------+-------------+------------+--------------------------------+--------------------------------+\n");
	fprintf(logFile,"| crewid1 | crewid2 | priority | startdate   | enddate    | name1                          | name2                          |\n");
	fprintf(logFile,"+---------+---------+----------+-------------+------------+--------------------------------+--------------------------------+\n");
	fprintf(logFile,"| %7d | %7d | %8d | %s | %s | %-30s | %-30s |",
		ppcPtr->crewid1,
		ppcPtr->crewid2,
		ppcPtr->priority,
    		(ppcPtr->startdate) ? dt_DateTimeToDateTimeString(ppcPtr->startdate,tbuf1,"%Y/%m/%d") : "0000/00/00",
    		(ppcPtr->enddate) ? dt_DateTimeToDateTimeString(ppcPtr->enddate,tbuf2,"%Y/%m/%d") : "0000/00/00",
		ppcPtr->name1,
		ppcPtr->name2 ? ppcPtr->name2 : "");
	for(mask = 1; mask != Bit_Stop; mask <<= 1) {
		if(testBit(ppcPtr->catIds,mask)) {
			fprintf(logFile,"%s%s",
				(didOne) ? ", " : " ",
				postToPostNameText(catBitToPost(mask),Post_Text));
			didOne = 1;
		}
	}
	fprintf(logFile,"\n");
	fprintf(logFile,"+---------+---------+----------+-------------+------------+--------------------------------+--------------------------------+\n");
}

/********************************************************************************
*	Function   readCrewIDS()
*	Purpose:  Read in crew id info and base airport id
********************************************************************************/
static int
readCrewIDS(MY_CONNECTION *myconn)
{

	extern char *crewIdSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	BINTREENODE *tmp;

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	CrewID *cidPtr;

	if(!myDoQuery(myconn, crewIdSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readCrewIDS(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readCrewIDS(): 0 rows returned.\n");
		return(0);
	}

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		cidPtr = textToCrewID(row);
		if(! cidPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readCrewIDS().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(!(empnbrRoot = RBTreeInsert(empnbrRoot, cidPtr, empnbrCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewIDS().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(!(crewidRoot = RBTreeInsert(crewidRoot, cidPtr, crewidCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCrewIDS().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		++bwCrewIDrecordCount;
	}

	if(verbose) {
		logMsg(logFile,"crew IDs:\n");
		fprintf(logFile,"+-------------+----------------+---------------+-------+------------------------------------+\n");
		fprintf(logFile,"|      crewid | employeenumber | baseairportid | icao  | fileas                             |\n");
		fprintf(logFile,"+-------------+----------------+---------------+-------+------------------------------------+\n");
		for(tmp = Minimum(empnbrRoot); tmp; tmp = Successor(tmp)) {
			cidPtr = getTreeObject(tmp);
			fprintf(logFile,"| %11d | %-14s | %13d | %-5s | %-34s |\n",
				cidPtr->crewid,
				cidPtr->employeenumber,
				cidPtr->baseairportid,
				cidPtr->baseicao,
				cidPtr->fileas);
		}
		fprintf(logFile,"+-------------+----------------+---------------+-------+------------------------------------+\n\n\n\n");
		fflush(logFile);
	}

	// free mysql results
	mysql_free_result(res);
	return 0;

}

/********************************************************************************
*	Function   readIntnlCert()
*	Purpose:  Read in aircraft with international certifications
********************************************************************************/
static int
readIntnlCert(MY_CONNECTION *myconn)
{
	extern char *intnlCertSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	BINTREENODE *tmp;

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	int *acidPtr;

	if(!myDoQuery(myconn, intnlCertSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readIntnlCert(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readIntnlCert(): 0 rows returned.\n");
		return(0);
	}

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		if(intnlCertCount == MAX_PLANES) {
			logMsg(logFile,"%s Line %d, limit exceeded in readIntnlCert().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		acidPtr = &intnlCertList[intnlCertCount++];
		*acidPtr = atoi(row[0]);
		if(!(intnlCertRoot = RBTreeInsert(intnlCertRoot, acidPtr, integerCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readIntnlCert().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}

	if(verbose) {
		logMsg(logFile,"International Certified Aircraft IDs:\n");
		fprintf(logFile,"+-------------+\n");
		fprintf(logFile,"|  aircraftid |\n");
		fprintf(logFile,"+-------------+\n");
		for(tmp = Minimum(intnlCertRoot); tmp; tmp = Successor(tmp)) {
			acidPtr = getTreeObject(tmp);
			fprintf(logFile,"| %11d |\n", *acidPtr);
		}
		fprintf(logFile,"+-------------+\n\n\n\n");
		fflush(logFile);
	}

	// free mysql results
	mysql_free_result(res);
	return 0;
}

///***********************************************************************************************************
//*	Function   readIntnlCertFromOracle()
//*	Purpose:  Read in aircraft with international certifications from data pulled directly from Oracle
//***********************************************************************************************************/
//static int
//readIntnlCertFromOracle(void)
//{
//
//	int wc;
//	char *wptrs[64];
//	BINTREENODE *tmp;
//	FILE *inp;
//	char buf[1024];
//	int *acidPtr;
//
//	if(!(inp = fopen("oracle/intnlCert.txt", "r"))) {
//		logMsg(logFile,"%s Line %d, can't open \"oracle/intnlCert.txt\"",__FILE__,__LINE__);
//		writeWarningData(myconn); exit(1);
//	}
//	while(fgets(buf, sizeof(buf) -1, inp)) {
//		wc = splitA(buf, "\t\r\n", wptrs);
//
//		if(intnlCertCount == MAX_PLANES) {
//			logMsg(logFile,"%s Line %d, limit exceeded in readIntnlCert().\n", __FILE__,__LINE__);
//			writeWarningData(myconn); exit(1);
//		}
//		acidPtr = &intnlCertList[intnlCertCount++];
//		*acidPtr = atoi(wptrs[0]);
//		if(!(intnlCertRoot = RBTreeInsert(intnlCertRoot, acidPtr, integerCmp))) {
//			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readIntnlCert().\n",__FILE__,__LINE__);
//			writeWarningData(myconn); exit(1);
//		}
//	}
//
//	if(verbose) {
//		logMsg(logFile,"International Certified Aircraft IDs:\n");
//		fprintf(logFile,"+-------------+\n");
//		fprintf(logFile,"|  aircraftid |\n");
//		fprintf(logFile,"+-------------+\n");
//		for(tmp = Minimum(intnlCertRoot); tmp; tmp = Successor(tmp)) {
//			acidPtr = getTreeObject(tmp);
//			fprintf(logFile,"| %11d |\n", *acidPtr);
//		}
//		fprintf(logFile,"+-------------+\n\n\n\n");
//		fflush(logFile);
//	}
//	fclose(inp);
//	return 0;
//}

typedef enum { CSownerid = 0, CStslc, CSchartercount, CStripcount, CSend_of_list } CSchartstats;
/********************************************************************************
*	Function readCharterStats()
*	Purpose: Read in charter statistics
********************************************************************************/
static int
readCharterStats(MY_CONNECTION *myconn)
{

	extern char *charterStatsSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	BINTREENODE *tmp;

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	CharterStats *csPtr, *csPtr2;
	Owner *cPtr;

	if(!myDoQuery(myconn, charterStatsSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readCharterStats(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readCharterStats(): 0 rows returned.\n");
		return(0);
	}
#ifdef DEBUGGING
	logMsg(logFile,"raw query results for charterStatsSQL:\n");
	if (verbose) displayResults(res, cols);
	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
#endif // DEBUGGING

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		csPtr = calloc((size_t) 1, sizeof(CharterStats));
		if(! csPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readCharterStats().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		csPtr->ownerid = row[CSownerid] ? atoi(row[CSownerid]) : 0;
		//csPtr->contractid = row[CScontractid] ? atoi(row[CScontractid]) : 0;
		csPtr->trips_since_last_charter = row[CStslc] ? atoi(row[CStslc]) : 0;
		csPtr->charter_count = row[CSchartercount] ? atoi(row[CSchartercount]) : 0;
		csPtr->trip_count = row[CStripcount] ? atoi(row[CStripcount]) : 0;
		if(! (tmp = TreeSearch(charterStatsRoot, csPtr, charterStatsCmp))) {
			if(!(charterStatsRoot = RBTreeInsert(charterStatsRoot, csPtr, charterStatsCmp))) {
				logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCharterStats(). ownerid: %d\n",__FILE__,__LINE__, csPtr->ownerid);
				writeWarningData(myconn); exit(1);
			}
			++charterStatsRecordCount;
		}
		else {
			// combine counts since same contract
			csPtr2 = (CharterStats *) getTreeObject(tmp);
			csPtr2->trips_since_last_charter += csPtr->trips_since_last_charter;
			csPtr2->charter_count += csPtr->charter_count;
			csPtr2->trip_count += csPtr->trip_count;
			// free(csPtr); ??
		}
	}
	if(verbose) {
		logMsg(logFile,"charter stats:\n");
		fprintf(logFile,"+---------+---------+---------+-------+\n");
		fprintf(logFile,"|         | trips   |         |       |\n");
		fprintf(logFile,"|         | since   |         |       |\n");
		fprintf(logFile,"|         | last    | charter | trip  |\n");
		fprintf(logFile,"| ownerid | charter | count   | count |\n");
		fprintf(logFile,"+---------+---------+---------+-------+\n");
		for(tmp = Minimum(charterStatsRoot); tmp; tmp = Successor(tmp)) {
			csPtr = getTreeObject(tmp);
			fprintf(logFile,"| %7d | %7d | %7d | %5d |\n",
				csPtr->ownerid,
				csPtr->trips_since_last_charter,
				csPtr->charter_count,
				csPtr->trip_count);
		}
		fprintf(logFile,"+---------+------------+---------+---------+-------+\n\n");
		fflush(logFile);
	}

	// free mysql results
	mysql_free_result(res);

	// allocate contractList
	ownerList = calloc((size_t) charterStatsRecordCount + 1, sizeof(Owner));
	cPtr = ownerList;
	for(tmp = Minimum(charterStatsRoot); tmp; tmp = Successor(tmp)) {
		csPtr = getTreeObject(tmp);
		cPtr->ownerID = csPtr->ownerid;
		if((csPtr->trip_count >= 6 && csPtr->charter_count == 0) || (csPtr->trip_count >= 6 && csPtr->trips_since_last_charter > 8))
			cPtr->charterLevel = 1;
		else if(csPtr->trip_count >= 6 && (csPtr->trips_since_last_charter > 6 && csPtr->trips_since_last_charter <= 8))
			cPtr->charterLevel = 2;
		else if((csPtr->trip_count < 6) || (csPtr->trip_count >= 6 && csPtr->trips_since_last_charter <= 6))
			cPtr->charterLevel = 3;
		else
			cPtr->charterLevel = 3;
		++cPtr;
	}
	if(verbose) {
		logMsg(logFile,"contract / charter level:\n");
		fprintf(logFile,"+------------+--------------+\n");
		fprintf(logFile,"|    ownerid | charterLevel |\n");
		fprintf(logFile,"+------------+--------------+\n");
		for(cPtr = ownerList; cPtr->ownerID; cPtr++)
			fprintf(logFile,"| %10d | %12d |\n", cPtr->ownerID, cPtr->charterLevel);
		fprintf(logFile,"+------------+--------------+\n\n");
	}
	numOwners = charterStatsRecordCount;
	return 0;

}


/********************************************************************************
*	Function   readBwCrewPairData()
*	Purpose:  read in BW crew pair data to fix hasFlownFirst bug  --Roger
********************************************************************************/
static int
readBwCrewPairData(MY_CONNECTION *myconn)
{

	extern char *bwCrewPairDataSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	CrewData *crwPtr;
	CrewData *crwPtr1;
	int previousCrewPosition = 0;
	int previousCrewAircraftid = 0;
	int bwCrewPairIndex;

	if(!myDoQuery(myconn, bwCrewPairDataSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readBwCrewPairData(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readBwCrewPairData(): 0 rows returned.\n");
		return(0);
	}

#ifdef DEBUGGING
	logMsg(logFile,"raw query results for bwCrewPairDataSQL:\n");
	if (verbose) displayResults(res, cols);
	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
#endif // DEBUGGING

	if(!(bwCrewPairList = (CrewData *) calloc((size_t) rowCount + 1, sizeof(CrewData)))) {
		logMsg(logFile,"%s Line %d, Out of Memory in readBwCrewPairData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	crwPtr1 = bwCrewPairList;
	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		crwPtr = textToCrewData(row);
		if(! crwPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readBwCrewPairData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		if( previousCrewAircraftid == crwPtr->aircraftid && previousCrewPosition == crwPtr->position )
			continue;
		crwPtr1->starttime = crwPtr->starttime;
		crwPtr1->endtime = crwPtr->endtime;
		crwPtr1->crewid = crwPtr->crewid;
		crwPtr1->crewassignmentid = crwPtr->crewassignmentid;
		crwPtr1->aircraftid = crwPtr->aircraftid;
		crwPtr1->position = crwPtr->position;
		crwPtr1->scenarioid = crwPtr->scenarioid;
		previousCrewPosition = crwPtr->position;
		previousCrewAircraftid = crwPtr->aircraftid;
		++crwPtr1;
		++bwPairRecordCount;
	}

	if(verbose) {
		logMsg(logFile,"BW Crew Pair Data:\n");
		showCrewDataHeader("Bitwise crew data sorted by aircraftid");
		crwPtr1 = bwCrewPairList;
		for(bwCrewPairIndex = 0; bwCrewPairIndex < bwPairRecordCount; bwCrewPairIndex++) {
			showCrewData(crwPtr1);
			crwPtr1++;
		}
		showCrewDataFooter();	

		fflush(logFile);
	}

	// free mysql results
	mysql_free_result(res);
	return 0;

}

/********************************************************************************
*	Function   readBwCrewData()
*	Purpose:  read in BW crew data
********************************************************************************/
static int
readBwCrewData(MY_CONNECTION *myconn)
{

	extern char *bwCrewDataSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	BINTREENODE *tmp;

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	CrewData *crwPtr;

	if(!myDoQuery(myconn, bwCrewDataSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readBwCrewData(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readBwCrewData(): 0 rows returned.\n");
		return(0);
	}

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		crwPtr = textToCrewData(row);

		crwPtr->actual_starttime=0;  //Jintao's temporary change 10/02/2007
		crwPtr->actual_endtime=0;    //Jintao's temporary chang  10/02/2007

		if(! crwPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readBwCrewData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(!(empnbrStarttimeActypRegRoot = RBTreeInsert(empnbrStarttimeActypRegRoot, crwPtr, empnbrStarttimeActypRegCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readBwCrewData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(!(crewidAcidStarttimeRoot = RBTreeInsert(crewidAcidStarttimeRoot, crwPtr, crewidAcidStarttimeCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readBwCrewData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(!(acidStarttimeEndtimeCrewidRoot = RBTreeInsert(acidStarttimeEndtimeCrewidRoot, crwPtr, acidStarttimeEndtimeCrewidCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readBwCrewData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
        //*************************
        if(!(crewidStarttimeEndtimeAcidRoot = RBTreeInsert(crewidStarttimeEndtimeAcidRoot, crwPtr, crewidStarttimeEndtimeAcidCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readBwCrewData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
        //*************************   Jintao's change 10/01/2007

		++bwRecordCount;
	}

	if(verbose) {
		logMsg(logFile,"BW Crew Data:\n");
		showCrewDataHeader("Bitwise crew data sorted by employeenumber, starttime, aircraftType, registration");
		for(tmp = Minimum(empnbrStarttimeActypRegRoot); tmp; tmp = Successor(tmp)) {
			crwPtr = (CrewData *) getTreeObject(tmp);
			showCrewData(crwPtr);
		}
		showCrewDataFooter();
	
		////////////////////////////////////////////////////////////////////////////////////////////////////
		showCrewDataHeader("Bitwise crew data sorted by crewid, aircraftid, starttime");
		for(tmp = Minimum(crewidAcidStarttimeRoot); tmp; tmp = Successor(tmp)) {
			crwPtr = (CrewData *) getTreeObject(tmp);
			showCrewData(crwPtr);
		}
		showCrewDataFooter();
		fflush(logFile);
	}

	// free mysql results
	mysql_free_result(res);
	return 0;

}

/********************************************************************************
*	Function  readPeakDays()
*	Purpose:  read in cs_peak_days table
********************************************************************************/
static int
readPeakDays(MY_CONNECTION *myconn)
{

	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	BINTREENODE *tmp;
	char tbufd[32];

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	PeakDay *pdPtr;

	if(!myDoQuery(myconn, "select level_id, peak_day from cs_peak_days", &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readPeakDays(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readPeakDays(): 0 rows returned.\n");
		return(0);
	}

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		if(!(pdPtr = (PeakDay *) calloc(1, sizeof(PeakDay)))) {
			logMsg(logFile,"%s Line %d, Out of Memory in readPeakDays().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		pdPtr->level_id = atoi(row[0]);
		pdPtr->peak_day = dt_StringToDate("YMD", "%d/%d/%d %*s", row[1], NULL);

		if(!(peakDayRoot = RBTreeInsert(peakDayRoot, pdPtr, peakDayCompare))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readPeakDays().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		++peakDayCount;
	}

	if(verbose) {
		logMsg(logFile,"\n\nPeak Days:\n");
		for(tmp = Minimum(peakDayRoot); tmp; tmp = Successor(tmp)) {
			pdPtr = (PeakDay *) getTreeObject(tmp);
			fprintf(logFile,"%3d %s\n", pdPtr->level_id, dt_DateToDateString(pdPtr->peak_day, tbufd, "%Y/%m/%d"));
		}
		fprintf(logFile,"\n\n");
		fflush(logFile);
	}

	// free mysql results
	mysql_free_result(res);
	return 0;

}

///**************************************************************************************
//*	Function  readPeakDaysFromOracle()
//*	Purpose:  read in cs_peak_days data that was pulled in directly from Oracle 
//**************************************************************************************/
//static int
//readPeakDaysFromOracle(void)
//{
//
//	int wc;
//	char *wptrs[64];
//	BINTREENODE *tmp;
//	FILE *inp;
//	char buf[1024];
//	char tbufd[32];
//
//
//	PeakDay *pdPtr;
//
//	if(!(inp = fopen("oracle/cs_peak_days.txt", "r"))) {
//		logMsg(logFile,"%s Line %d, can't open \"oracle/cs_peak_days.txt\"",__FILE__,__LINE__);
//		writeWarningData(myconn); exit(1);
//	}
//	while(fgets(buf, sizeof(buf) -1, inp)) {
//		wc = splitA(buf, "\t\r\n", wptrs);
//		if(!(pdPtr = (PeakDay *) calloc(1, sizeof(PeakDay)))) {
//			logMsg(logFile,"%s Line %d, Out of Memory in readPeakDays().\n", __FILE__,__LINE__);
//			writeWarningData(myconn); exit(1);
//		}
//		pdPtr->level_id = atoi(wptrs[0]);
//		pdPtr->peak_day = dt_StringToDate("YMD", "%d/%d/%d %*s", wptrs[1], NULL);
//
//		if(!(peakDayRoot = RBTreeInsert(peakDayRoot, pdPtr, peakDayCompare))) {
//			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readPeakDays().\n",__FILE__,__LINE__);
//			writeWarningData(myconn); exit(1);
//		}
//		++peakDayCount;
//	}
//
//	if(verbose) {
//		logMsg(logFile,"\n\nPeak Days:\n");
//		for(tmp = Minimum(peakDayRoot); tmp; tmp = Successor(tmp)) {
//			pdPtr = (PeakDay *) getTreeObject(tmp);
//			fprintf(logFile,"%3d %s\n", pdPtr->level_id, dt_DateToDateString(pdPtr->peak_day, tbufd, "%Y/%m/%d"));
//		}
//		fprintf(logFile,"\n\n");
//		fflush(logFile);
//	}
//	fclose(inp);
//	return 0;
//
//}

/********************************************************************************
*	Function  readPeakDaysContractRates()
*	Purpose:  read in cs_peak_days_contract_rates table
********************************************************************************/
static int
readPeakDaysContractRates(MY_CONNECTION *myconn)
{

	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	BINTREENODE *tmp;

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	PeakDayContractRate  *pdcrPtr;

	if(!myDoQuery(myconn, "select contractid, level_id, hourly_rate, flex_hours from cs_peak_days_contract_rates", &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readPeakDaysContractRates(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readPeakDaysContractRates(): 0 rows returned.\n");
		return(0);
	}
	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		if(!(pdcrPtr = (PeakDayContractRate *) calloc(1, sizeof(PeakDayContractRate)))) {
			logMsg(logFile,"%s Line %d, Out of Memory in readPeakDaysContractRates().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		pdcrPtr->contractid = atoi(row[0]);
		pdcrPtr->level_id = atoi(row[1]);
		pdcrPtr->hourly_rate = atof(row[2]);
		pdcrPtr->flex_hours = atof(row[3]);

		if(!(peakDayContractRateRoot = RBTreeInsert(peakDayContractRateRoot, pdcrPtr, peakDayContractRatesCompare))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readPeakDaysContractRates().\n",__FILE__,__LINE__);
			writeWarningData(myconn); 
			exit(1);
		}
		++peakDayContractRateCount;
	}

	if(verbose) {
		logMsg(logFile,"\n\nPeak Day / Contract Rates:\n");
		fprintf(logFile,"        hr  flx\n");
		fprintf(logFile,"ct  lvl rt  hrs\n");
		for(tmp = Minimum(peakDayContractRateRoot); tmp; tmp = Successor(tmp)) {
			pdcrPtr = (PeakDayContractRate *) getTreeObject(tmp);
			fprintf(logFile,"%3d %3d %2.2f %2.2f\n", pdcrPtr->contractid, pdcrPtr->level_id, pdcrPtr->hourly_rate, pdcrPtr->flex_hours);
		}
		fprintf(logFile,"\n\n");
	}

	// free mysql results
	mysql_free_result(res);
	return 0;
}

/*******************************************************************************************************
*	Function  readPeakDaysContractRatesFromOracle()
*	Purpose:  read in cs_peak_days_contract_rates table from date pulled in directly from Oracle
*******************************************************************************************************/
static int
readPeakDaysContractRatesFromOracle(void)
{
	int wc;
	char *wptrs[64];
	BINTREENODE *tmp;
	FILE *inp;
	char buf[1024];


	PeakDayContractRate  *pdcrPtr;

	if(!(inp = fopen("oracle/cs_peak_days_contract_rates.txt", "r"))) {
		logMsg(logFile,"%s Line %d, can't open \"oracle/cs_peak_days_contract_rates.txt\"",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	while(fgets(buf, sizeof(buf) -1, inp)) {
		wc = splitA(buf, "\t\r\n", wptrs);

		if(!(pdcrPtr = (PeakDayContractRate *) calloc(1, sizeof(PeakDayContractRate)))) {
			logMsg(logFile,"%s Line %d, Out of Memory in readPeakDaysContractRatesFromOracle().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		pdcrPtr->contractid = atoi(wptrs[0]);
		pdcrPtr->level_id = atoi(wptrs[1]);
		pdcrPtr->hourly_rate = atof(wptrs[2]);
		pdcrPtr->flex_hours = atof(wptrs[3]);

		if(!(peakDayContractRateRoot = RBTreeInsert(peakDayContractRateRoot, pdcrPtr, peakDayContractRatesCompare))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readPeakDaysContractRatesFromOracle().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		++peakDayContractRateCount;
	}

	if(verbose) {
		logMsg(logFile,"\n\nPeak Day / Contract Rates:\n");
		fprintf(logFile,"        hr  flx\n");
		fprintf(logFile,"ct  lvl rt  hrs\n");
		for(tmp = Minimum(peakDayContractRateRoot); tmp; tmp = Successor(tmp)) {
			pdcrPtr = (PeakDayContractRate *) getTreeObject(tmp);
			fprintf(logFile,"%3d %3d %2.2f %2.2f\n", pdcrPtr->contractid, pdcrPtr->level_id, pdcrPtr->hourly_rate, pdcrPtr->flex_hours);
		}
		fprintf(logFile,"\n\n");
	}
	fclose(inp);
	return 0;
}

/*******************************************************************************************************
*	Function  readPeakDaysContractRatesFromOracleDB()
*	Purpose:  read in cs_peak_days_contract_rates table directly from Oracle
*******************************************************************************************************/
typedef enum {
	contractid = 0, level_id, hourly_rate, flex_hours
} peakdaycontractratesColumns;

static int
readPeakDaysContractRatesFromOracleDB(ORACLE_SOCKET *orl_socket)
{
    extern char *peakdayscontractratesOracleSQL; 
	PeakDayContractRate  *pdcrPtr;
	BINTREENODE *tmp;

	if(Orlconnection_doquery(orl_socket, peakdayscontractratesOracleSQL))
	{  logMsg(logFile,"%s Line %d: failed to execute query /*peakdayscontractratesOracleSQL*/ and get data\n", __FILE__,__LINE__);
	   exit(1);
	}  
    //{  
		//   printf("fetched demandid %s and reservationid %s\n",orl_socket->results[0],orl_socket->results[1]);
	  // }

	while(Orlconnection_fetch(orl_socket)==0) {
		if(!(pdcrPtr = (PeakDayContractRate *) calloc(1, sizeof(PeakDayContractRate)))) {
			logMsg(logFile,"%s Line %d, Out of Memory in readPeakDaysContractRatesFromOracleDB().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		pdcrPtr->contractid = atoi(orl_socket->results[contractid]);
		pdcrPtr->level_id = atoi(orl_socket->results[level_id]);
		pdcrPtr->hourly_rate = atof(orl_socket->results[hourly_rate]);
		pdcrPtr->flex_hours = atof(orl_socket->results[flex_hours]);

		if(!(peakDayContractRateRoot = RBTreeInsert(peakDayContractRateRoot, pdcrPtr, peakDayContractRatesCompare))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readPeakDaysContractRatesFromOracleDB().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		++peakDayContractRateCount;
	}

	if(verbose) {
		logMsg(logFile,"\n\nPeak Day / Contract Rates:\n");
		fprintf(logFile,"        hr  flx\n");
		fprintf(logFile,"ct  lvl rt  hrs\n");
		for(tmp = Minimum(peakDayContractRateRoot); tmp; tmp = Successor(tmp)) {
			pdcrPtr = (PeakDayContractRate *) getTreeObject(tmp);
			fprintf(logFile,"%3d %3d %2.2f %2.2f\n", pdcrPtr->contractid, pdcrPtr->level_id, pdcrPtr->hourly_rate, pdcrPtr->flex_hours);
		}
		fprintf(logFile,"\n\n");
	}
	return 0;
}
/********************************************************************************
*	Function   readCsCrewData()
*	Purpose:  read in cs_crew_data
********************************************************************************/
static int
readCsCrewData(MY_CONNECTION *myconn)
{

	extern char *csCrewDataSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	BINTREENODE *tmp, *oldTree;
	//unsigned long t1, t2;

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	CS_CrewData *crwPtr;

	//t2 = (unsigned long) ((pwStartDate & DateOnly) >> 32);
	//fprintf(logFile,"t2 set to %lu\n", t2);//debug

	if(!myDoQuery(myconn, csCrewDataSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readCsCrewData(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readCsCrewData(): 0 rows returned.\n");
		return(0);
	}
#ifdef DEBUGGING
	logMsg(logFile,"raw query results for csCrewDataSQL:\n");
	if (verbose) displayResults(res, cols);
	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
#endif // DEBUGGING
	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		crwPtr = textToCsCrewData(row);
		if(! crwPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

        oldTree = cs_crew_dataRoot;		
		if(!(cs_crew_dataRoot = RBTreeInsert(cs_crew_dataRoot, crwPtr, cs_crew_data_crewidScheduledOnStarttimeCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCsCrewData().\n",__FILE__,__LINE__);
			showCsCrewDataHeader("Insert Failed");
			showCsCrewData(crwPtr);
			showCsCrewDataFooter();
			writeWarningData(myconn);
			cs_crew_dataRoot = oldTree;
			continue;
			//exit(1);
		}
		++csRecordCount;
	}

	if(verbose) {
		logMsg(logFile,"CS Crew Data:\n");
		showCsCrewDataHeader("CS Crew Data");
		for(tmp = Minimum(cs_crew_dataRoot); tmp; tmp = Successor(tmp)) {
			crwPtr = (CS_CrewData *) getTreeObject(tmp);
			showCsCrewData(crwPtr);
		}
		showCsCrewDataFooter();
	
		fflush(logFile);
	}

	// free mysql results
	mysql_free_result(res);
	return 0;

}

/********************************************************************************
*	Function   readCsCrewDataFromOracle()
*	Purpose:  read in cs_crew_data that were pulled directly from Oracle
********************************************************************************/
static int
readCsCrewDataFromOracle(void)
{

	int wc;
	char *wptrs[64];
	BINTREENODE *tmp, *oldTree;
	FILE *inp;
	char buf[1024];


	CS_CrewData *crwPtr;

	if(!(inp = fopen("oracle/cs_crew_duty.txt", "r"))) {
		logMsg(logFile,"%s Line %d, can't open \"oracle/cs_crew_duty.txt\"",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	while(fgets(buf, sizeof(buf) -1, inp)) {
		wc = splitA(buf, "\t\r\n", wptrs);

		crwPtr = textToCsCrewData(wptrs);
		if(! crwPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracle().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		oldTree = cs_crew_dataRoot;
		
		if(!(cs_crew_dataRoot = RBTreeInsert(cs_crew_dataRoot, crwPtr, cs_crew_data_crewidScheduledOnStarttimeCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCsCrewData().\n",__FILE__,__LINE__);
			showCsCrewDataHeader("Insert Failed");
			showCsCrewData(crwPtr);
			showCsCrewDataFooter();
			writeWarningData(myconn); 
			cs_crew_dataRoot = oldTree;
			continue;
			//exit(1);
		}
		++csRecordCount;
	}

	if(verbose) {
		logMsg(logFile,"CS Crew Data:\n");
		showCsCrewDataHeader("CS Crew Data");
		for(tmp = Minimum(cs_crew_dataRoot); tmp; tmp = Successor(tmp)) {
			crwPtr = (CS_CrewData *) getTreeObject(tmp);
			showCsCrewData(crwPtr);
		}
		showCsCrewDataFooter();
	
		fflush(logFile);
	}
	fclose(inp);
	return(0);
}
/********************************************************************************
*	Function   readCsCrewDataFromOracleDB()
*	Purpose:  read in cs_crew_data directly from Oracle
********************************************************************************/
typedef enum {
	crewid =0, starttime, endtime, ca_aircraftid, cd_aircraftid,ca_registration, cd_registration, position, scheduled_on, scheduled_off, actual_on, actual_off,
currdate, lastupdated, scenarioid
} cscrewDataOracleColumns;

static int
readCsCrewDataFromOracleDB(ORACLE_SOCKET *orl_socket)
{
	
	BINTREENODE *tmp, *oldTree;
	CS_CrewData *cdPtr;
	int errNbr;
	extern char *csCrewDataOracleSQL;

	if(Orlconnection_doquery(orl_socket, csCrewDataOracleSQL))
	{  logMsg(logFile,"%s Line %d: failed to execute query /*csCrewDataOracleSQL*/ and get data\n", __FILE__,__LINE__);
	   exit(1);
	}  

	while(Orlconnection_fetch(orl_socket)==0) {
		if(!(cdPtr = (CS_CrewData *) calloc(1, sizeof(CS_CrewData)))) {
			logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(!(cdPtr->crewid = atoi(orl_socket->results[crewid]))) {
		    logMsg(logFile,"%s Line %d, zero aircraftTypeID in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if((cdPtr->starttime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket->results[starttime], NULL, &errNbr)) == BadDateTime) {
		    logMsg(logFile,"%s Line %d, Bad date in readCsCrewDataFromOracleDB(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, orl_socket->results[starttime]);
		    writeWarningData(myconn); exit(1);
	    }
	    if((cdPtr->endtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket->results[endtime], NULL, &errNbr)) == BadDateTime) {
		    logMsg(logFile,"%s Line %d, Bad date in readCsCrewDataFromOracleDB(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, orl_socket->results[endtime]);
		    writeWarningData(myconn); exit(1);
	    }
	    if(!(cdPtr->ca_aircraftid = atoi(orl_socket->results[ca_aircraftid]))) {
		    logMsg(logFile,"%s Line %d, zero ca_aircraftTypeID in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if(!(cdPtr->cd_aircraftid = atoi(orl_socket->results[cd_aircraftid]))) {
		    logMsg(logFile,"%s Line %d, zero cd_aircraftTypeID in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if(!(cdPtr->ca_registration = strdup(orl_socket->results[ca_registration]))) {
		    logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if(!(cdPtr->cd_registration = strdup(orl_socket->results[cd_registration]))) {
		    logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB()().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if(!(cdPtr->position = atoi(orl_socket->results[position]))) {
		    logMsg(logFile,"%s Line %d, zero position in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if(((oracleDirect && strlen(orl_socket->results[scheduled_on]))) && strcmp(orl_socket->results[scheduled_on], "1901/01/01 00:00") != 0) {
		   if((cdPtr->scheduled_on = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket->results[scheduled_on], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in readCsCrewDataFromOracleDB(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, orl_socket->results[scheduled_on]);
			writeWarningData(myconn); exit(1);
		  }
		}
	    if(((oracleDirect && strlen(orl_socket->results[scheduled_off]))) && strcmp(orl_socket->results[scheduled_off], "1901/01/01 00:00") != 0) {
		   if((cdPtr->scheduled_off = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket->results[scheduled_off], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in readCsCrewDataFromOracleDB(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, orl_socket->results[scheduled_off]);
			writeWarningData(myconn); exit(1);
		   }
	     }
	    if(((oracleDirect && strlen(orl_socket->results[actual_on]))) && strcmp(orl_socket->results[actual_on], "1901/01/01 00:00") != 0) {
		   if((cdPtr->actual_on = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket->results[actual_on], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in readCsCrewDataFromOracleDB(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, orl_socket->results[actual_on]);
			writeWarningData(myconn); exit(1);
		   }
	    }
	    if(((oracleDirect && strlen(orl_socket->results[actual_off]))) && strcmp(orl_socket->results[actual_off], "1901/01/01 00:00") != 0) {
		   if((cdPtr->actual_off = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket->results[actual_off], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in readCsCrewDataFromOracleDB(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, orl_socket->results[actual_off]);
			writeWarningData(myconn); exit(1);
		   }
	    }
	    if(((oracleDirect && strlen(orl_socket->results[currdate]))) && strcmp(orl_socket->results[currdate], "1901/01/01 00:00") != 0) {
		   if((cdPtr->currdate = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket->results[currdate], NULL, &errNbr)) == BadDateTime) {
			logMsg(logFile,"%s Line %d, Bad date in readCsCrewDataFromOracleDB(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, orl_socket->results[currdate]);
			writeWarningData(myconn); exit(1);
		    }
	     }
	    if(oracleDirect && orl_socket->results[lastupdated]) {
		    if((cdPtr->lastupdated = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket->results[lastupdated], NULL, &errNbr)) == BadDateTime) {
			 logMsg(logFile,"%s Line %d, Bad date in readCsCrewDataFromOracleDB(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, orl_socket->results[lastupdated]);
			 writeWarningData(myconn); exit(1);
		    }
	    }
	   if(!(cdPtr->scenarioid = atoi(orl_socket->results[scenarioid]))) {
		    logMsg(logFile,"%s Line %d, zero scenarioid in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }

		//populate the scheduled times if the actual times are ready.  //Roger 02/20/2007
	    cdPtr->scheduled_on = cdPtr->actual_on ? cdPtr->actual_on : cdPtr->scheduled_on;
	    cdPtr->scheduled_off = cdPtr->actual_off ? cdPtr->actual_off : cdPtr->scheduled_off;


	    // populate the actual on/off if the scheduled on/off is before  the planning window 
	    pwStartDate = dt_time_tToDateTime(optParam.windowStart);
	    if (!cdPtr->actual_on && cdPtr->scheduled_on <=	pwStartDate)
		   cdPtr->actual_on = cdPtr->scheduled_on;
	
	    if (!cdPtr->actual_off && cdPtr->scheduled_off <= pwStartDate)
		   cdPtr->actual_off = cdPtr->scheduled_off;
	    oldTree = cs_crew_dataRoot;
		
        if(!(cs_crew_dataRoot = RBTreeInsert(cs_crew_dataRoot, cdPtr, cs_crew_data_crewidScheduledOnStarttimeCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCsCrewData().\n",__FILE__,__LINE__);
			showCsCrewDataHeader("Insert Failed");
			showCsCrewData(cdPtr);
			showCsCrewDataFooter();
			writeWarningData(myconn); 
			cs_crew_dataRoot = oldTree;
			continue;
			//exit(1);
		}
		++csRecordCount;
  }

	if(verbose) {
		logMsg(logFile,"CS Crew Data:\n");
		showCsCrewDataHeader("CS Crew Data");
		for(tmp = Minimum(cs_crew_dataRoot); tmp; tmp = Successor(tmp)) {
			cdPtr = (CS_CrewData *) getTreeObject(tmp);
			showCsCrewData(cdPtr);
		}
		showCsCrewDataFooter();
	    fflush(logFile);
	}
	return(0);
}
/********************************************************************************
*	Function   readSsCrewData()
*	Purpose:  read in Schedule Soft crew data
********************************************************************************/
static int
readSsCrewData(MY_CONNECTION *myconn)
{

	extern char *ssCrewDataSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	BINTREENODE *tmp;
	DateTime *datePtr;
	//char tbuf[64];
	int wc;

	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	SS_CrewData *ss_crwPtr;

	if(!myDoQuery(myconn, ssCrewDataSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readSsCrewData(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readSsCrewData(): 0 rows returned.\n");
		return(0);
	}

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		ss_crwPtr = textToSS_CrewData(row);
		if(! ss_crwPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readSsCrewData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(!(badgeDeptDateRoot = RBTreeInsert(badgeDeptDateRoot, ss_crwPtr, badgeDeptDateCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readSsCrewData().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		++ssRecordCount;
		if(! TreeSearch(dateListRoot, &(ss_crwPtr->dtdate), dateListCmp)) {
			datePtr = (DateTime *) calloc((size_t) 1, sizeof(DateTime));
			if(! datePtr) {
				logMsg(logFile,"%s Line %d, Out of Memory in readSsCrewData().\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			*datePtr = ss_crwPtr->dtdate;
			if(!(dateListRoot = RBTreeInsert(dateListRoot, datePtr, dateListCmp))) {
				logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readSsCrewData().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			++uniqueDateCount;
		}
	}

	if(verbose) {
		logMsg(logFile,"Schedule Soft Crew Data:\n");
		showSS_CrewDataHeader("Schedule Soft crew data sorted by zbadgeid, zdeptdesc, dtdate");
		for(tmp = Minimum(badgeDeptDateRoot); tmp; tmp = Successor(tmp)) {
			ss_crwPtr = (SS_CrewData *) getTreeObject(tmp);
			showSS_CrewData(ss_crwPtr);
		}
		showSS_CrewDataFooter();
		fflush(logFile);
	}

	/////////////////////////
	// set up dateList array
	/////////////////////////
	if(!(dateList = (DateTime *) calloc((size_t) uniqueDateCount, sizeof(DateTime)))) {
		if(! dateList) {
			logMsg(logFile,"%s Line %d, Out of Memory in readSsCrewData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	wc = 0;
	for(tmp = Minimum(dateListRoot); tmp; tmp = Successor(tmp)) {
		*(dateList + wc) = *((DateTime *) getTreeObject(tmp));
		//fprintf(logFile,"%s\n", dt_DateTimeToDateTimeString(*((DateTime *) getTreeObject(tmp)), tbuf, "%Y/%m/%d %H:%M"));
		++wc;
	}

	// free mysql results
	mysql_free_result(res);
	
	return 0;

}

/*****************************************************************************************
*	Function   readSsCrewDataFromOracle()
*	Purpose:  read in Schedule Soft crew data that was pulled directly from Oracle
*****************************************************************************************/
static int
readSsCrewDataFromOracle(void)
{
	int wc;
	char *wptrs[64];
	BINTREENODE *tmp;
	FILE *inp;
	char buf[1024];

	DateTime *datePtr;
	SS_CrewData *ss_crwPtr;

	if(!(inp = fopen("oracle/ssoftcrew.txt", "r"))) {
		logMsg(logFile,"%s Line %d, can't open \"oracle/ssoftcrew.txt\"",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	while(fgets(buf, sizeof(buf) -1, inp)) {
		wc = splitA(buf, "\t\r\n", wptrs);

		ss_crwPtr = textToSS_CrewData(wptrs);
		if(! ss_crwPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(!(badgeDeptDateRoot = RBTreeInsert(badgeDeptDateRoot, ss_crwPtr, badgeDeptDateCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed.\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		++ssRecordCount;
		if(! TreeSearch(dateListRoot, &(ss_crwPtr->dtdate), dateListCmp)) {
			datePtr = (DateTime *) calloc((size_t) 1, sizeof(DateTime));
			if(! datePtr) {
				logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			*datePtr = ss_crwPtr->dtdate;
			if(!(dateListRoot = RBTreeInsert(dateListRoot, datePtr, dateListCmp))) {
				logMsg(logFile,"%s Line %d, RBTreeInsert() failed.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			++uniqueDateCount;
		}
	}

	if(verbose) {
		logMsg(logFile,"Schedule Soft Crew Data:\n");
		showSS_CrewDataHeader("Schedule Soft crew data sorted by zbadgeid, zdeptdesc, dtdate");
		for(tmp = Minimum(badgeDeptDateRoot); tmp; tmp = Successor(tmp)) {
			ss_crwPtr = (SS_CrewData *) getTreeObject(tmp);
			showSS_CrewData(ss_crwPtr);
		}
		showSS_CrewDataFooter();
		fflush(logFile);
	}

	/////////////////////////
	// set up dateList array
	/////////////////////////
	if(!(dateList = (DateTime *) calloc((size_t) uniqueDateCount, sizeof(DateTime)))) {
		if(! dateList) {
			logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	wc = 0;
	for(tmp = Minimum(dateListRoot); tmp; tmp = Successor(tmp)) {
		*(dateList + wc) = *((DateTime *) getTreeObject(tmp));
		//fprintf(logFile,"%s\n", dt_DateTimeToDateTimeString(*((DateTime *) getTreeObject(tmp)), tbuf, "%Y/%m/%d %H:%M"));
		++wc;
	}
	fclose(inp);
	return 0;
}

/*****************************************************************************************
*	Function   readSsCrewDataFromOracleDB()
*	Purpose:  read in Schedule Soft crew data directly from Oracle
*****************************************************************************************/
typedef enum {
zbadgeid = 0, zdeptdesc, dtdate, zlname, zfname, zmname, lempid, lempinfoid, lpostid, zpostdesc, zshiftdesc, zacccodeid, 
zacccodedesc, znote
} ssCrewDateOracleColumns;

static int
readSsCrewDataFromOracleDB(ORACLE_SOCKET *orl_socket)
{
	int wc;
	BINTREENODE *tmp;
	int errNbr;
	char tbuf1[8192];
	char tbuf2[8192];
	char *wptrs[256];
    DateTime *datePtr;
	SS_CrewData *ssCdPtr;
	extern char *ssCrewDataOracleSQL;


	if(Orlconnection_doquery(orl_socket, ssCrewDataOracleSQL))
	{  logMsg(logFile,"%s Line %d: failed to execute query /*ssCrewDataOracleSQL*/ and get data\n", __FILE__,__LINE__);
	   exit(1);
	}  

	while(Orlconnection_fetch(orl_socket)==0){
		if(!(ssCdPtr = (SS_CrewData *) calloc(1, sizeof(SS_CrewData)))) {
			logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		
        if(!(ssCdPtr->zbadgeid = strdup(orl_socket->results[zbadgeid]))) {
		    logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if(!(ssCdPtr->zdeptdesc = strdup(orl_socket->results[zdeptdesc]))) {
		    logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if((ssCdPtr->dtdate = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket->results[dtdate], NULL, &errNbr)) == BadDateTime) {
		    logMsg(logFile,"%s Line %d, Bad date in readCsCrewDataFromOracleDB(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, orl_socket->results[dtdate]);
		    writeWarningData(myconn); exit(1);
	    }
	    if(!(ssCdPtr->zlname = strdup(orl_socket->results[zlname]))) {
		    logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if(!(ssCdPtr->zfname = strdup(orl_socket->results[zfname]))) {
		    logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if(oracleDirect && strlen(orl_socket->results[zmname])) {
		    if(!(ssCdPtr->zmname = strdup(orl_socket->results[zmname]))) {
			   logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
			   writeWarningData(myconn); exit(1);
		     }
	    }
	    if(!(ssCdPtr->lempid = atoi(orl_socket->results[lempid]))) {
		    logMsg(logFile,"%s Line %d, zero lempid in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if(!(ssCdPtr->lempinfoid = atoi(orl_socket->results[lempinfoid]))) {
		    logMsg(logFile,"%s Line %d, zero lempinfoid in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if(!(ssCdPtr->lpostid = atoi(orl_socket->results[lpostid]))) {
		    logMsg(logFile,"%s Line %d, zero lpostid in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }
	    if(!(ssCdPtr->zpostdesc = strdup(orl_socket->results[zpostdesc]))) {
		    logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		    writeWarningData(myconn); exit(1);
	    }

/*
	if(!(ssCdPtr->zshiftdesc = strdup(row[SS_zshiftdesc]))) {
		logMsg(logFile,"%s Line %d, Out of Memory in textToSS_CrewData().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
*/
// fix: this fix goes with new SQL to read Oracle ScheduleSoft data.
// added a (+) to the shift-empjobs join in the ssoftcrew.sql file as follows:
// "and sh.lshiftid(+) = ej.lshiftid"
	    if(oracleDirect && orl_socket->results[zshiftdesc][0]) {
		   if(!(ssCdPtr->zshiftdesc = strdup(orl_socket->results[zshiftdesc]))) {
			   logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
			   writeWarningData(myconn); exit(1);
		   }
	    }
	    else {
		   if(strncasecmp(ssCdPtr->zpostdesc,"Assistant", strlen("Assistant")) == 0 || strncasecmp(ssCdPtr->zpostdesc,"Check", strlen("Check")) == 0 ||
		       strncasecmp(ssCdPtr->zpostdesc,"Standards", strlen("Standards")) == 0)
			   ssCdPtr->zshiftdesc = strdup("PIC");
		   else
			   ssCdPtr->zshiftdesc = strdup("SIC");
	    }
// end fix
	   if(!(ssCdPtr->zacccodeid = strdup(orl_socket->results[zacccodeid]))) {
		   logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		   writeWarningData(myconn); exit(1);
	   }
	   if(!(ssCdPtr->zacccodedesc = strdup(orl_socket->results[zacccodedesc]))) {
		   logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
		   writeWarningData(myconn); exit(1);
	   }
	   if(oracleDirect && strlen(orl_socket->results[znote])) {
		char *p;
		(void) hexToString(orl_socket->results[znote], tbuf1, sizeof(tbuf1) -1);
		p = tbuf1;
		while(*p) {
			if(*p == '\t' || *p == '\r' || *p == '\n')
				*p = ' ';
			++p;
		}
		(void) rmvExtraSpaces(tbuf1, tbuf2, wptrs);
		if(!(ssCdPtr->znote = strdup(tbuf2))) {
			logMsg(logFile,"%s Line %d, Out of Memory in readCsCrewDataFromOracleDB().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	   }
        if(!(badgeDeptDateRoot = RBTreeInsert(badgeDeptDateRoot, ssCdPtr, badgeDeptDateCmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed.\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		++ssRecordCount;
		if(! TreeSearch(dateListRoot, &(ssCdPtr->dtdate), dateListCmp)) {
			datePtr = (DateTime *) calloc((size_t) 1, sizeof(DateTime));
			if(! datePtr) {
				logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			*datePtr = ssCdPtr->dtdate;
			if(!(dateListRoot = RBTreeInsert(dateListRoot, datePtr, dateListCmp))) {
				logMsg(logFile,"%s Line %d, RBTreeInsert() failed.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			++uniqueDateCount;
		}
	}

	if(verbose) {
		logMsg(logFile,"Schedule Soft Crew Data:\n");
		showSS_CrewDataHeader("Schedule Soft crew data sorted by zbadgeid, zdeptdesc, dtdate");
		for(tmp = Minimum(badgeDeptDateRoot); tmp; tmp = Successor(tmp)) {
			ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
			showSS_CrewData(ssCdPtr);
		}
		showSS_CrewDataFooter();
		fflush(logFile);
	}

	/////////////////////////
	// set up dateList array
	/////////////////////////
	if(!(dateList = (DateTime *) calloc((size_t) uniqueDateCount, sizeof(DateTime)))) {
		if(! dateList) {
			logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	wc = 0;
	for(tmp = Minimum(dateListRoot); tmp; tmp = Successor(tmp)) {
		*(dateList + wc) = *((DateTime *) getTreeObject(tmp));
		//fprintf(logFile,"%s\n", dt_DateTimeToDateTimeString(*((DateTime *) getTreeObject(tmp)), tbuf, "%Y/%m/%d %H:%M"));
		++wc;
	}
	return 0;
}

/************************************************************************************************
*	Function	ssCrewDataProcess							Date last modified:  9/27/07 RLZ	*
*	Purpose:	Ssoft data post-process, adding crewid, base, and convert local working time to GMT.
*   Note: local working time is from crew duty policy coming as a parameter.
************************************************************************************************/

static void ssCrewDataProcess(void)
{
    BINTREENODE *tmp, *tmp1;
	SS_CrewData *ssCdPtr, *ssCdPtr0;
	CrewID *cdidPtr, cdidBuf;
	char curcrew[32];
    for(tmp = Minimum(badgeDeptDateRoot); tmp; ) {
		ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
		strcpy(curcrew, ssCdPtr->zbadgeid);
        cdidBuf.employeenumber = ssCdPtr->zbadgeid;
		tmp1 = TreeSearch(empnbrRoot, &cdidBuf, empnbrCmp);
		if(tmp1) {
			cdidPtr = getTreeObject(tmp1);
			ssCdPtr->crewID = cdidPtr->crewid;
			ssCdPtr->baseAirportID = cdidPtr->baseairportid;
			//fprintf(logFile,"| %16s ", 
			ssCdPtr->dtdate = dt_addToDateTime(Minutes, optParam.crewTourStartInMin + locFromGMTinMinutes + timeZoneAdjByApt(cdidPtr->baseairportid, ssCdPtr->dtdate)/60, ssCdPtr->dtdate);

			// updated all records with same badgeid
			if (Successor(tmp)){
				for(tmp = Successor(tmp); tmp; tmp = Successor(tmp)) {
					ssCdPtr0 = (SS_CrewData *) getTreeObject(tmp);
					if(strcasecmp(curcrew, ssCdPtr0->zbadgeid) == 0){
      					ssCdPtr0->crewID = cdidPtr->crewid;
						ssCdPtr0->baseAirportID = cdidPtr->baseairportid;
						//ssCdPtr0->dtdate += timeZoneAdjByApt(cdidPtr->baseairportid, ssCdPtr0->dtdate);
						ssCdPtr0->dtdate = dt_addToDateTime(Minutes, optParam.crewTourStartInMin + locFromGMTinMinutes + timeZoneAdjByApt(cdidPtr->baseairportid, ssCdPtr0->dtdate)/60, ssCdPtr0->dtdate);

					}
					else {  // a different crew, need to re-search empnbrRoot/flightcrew
						break;
					}
				}
			}
			else
				break;
		}
		else { //data is missing for this crew, warning			
			logMsg(logFile,"%s Line %d: can't find crewid for %s, %s .\n", __FILE__,__LINE__, ssCdPtr->zlname, ssCdPtr->zfname);
			writeWarningData(myconn); exit(1);
		}		
	}
	
	if(verbose) {
		logMsg(logFile,"Schedule Soft Crew Data with crewid, base, starttime:\n");
		showSS_CrewDataHeader("Schedule Soft crew data sorted by zbadgeid, zdeptdesc, dtdate");
		for(tmp = Minimum(badgeDeptDateRoot); tmp; tmp = Successor(tmp)) {
			ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
			showSS_CrewData(ssCdPtr);
		}
		showSS_CrewDataFooter();
		fflush(logFile);
	}	
}


//////////////////////////////////////////////////////////////////////
// end crew data collection
//////////////////////////////////////////////////////////////////////

static void
showAcListEntry(int aircraftID)
{
    Aircraft *tPtr;
	int errNbr;
	char tbuf[32];
	fprintf(logFile,"acList entry for aircraftid %d:\n", aircraftID);
	fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+\n");
	fprintf(logFile,"| aircraftID | aircraftTypeID | sequencePosn | availAirportID | ICAO  | availFboID | availDt          | maintFlag |\n");
	fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+\n");
	for(tPtr = acList; tPtr->aircraftID; ++tPtr) {
		if(tPtr->aircraftID == aircraftID) {
			fprintf(logFile,"| %10d | %14d | %12d | %14d | %5s | %10d | %16s | %9d |\n",
				tPtr->aircraftID,
				tPtr->aircraftTypeID,
				tPtr->sequencePosn,
				tPtr->availAirportID,
				tPtr->availAptICAO,
				tPtr->availFboID,
				(tPtr->availDT) ?
	    			dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				tPtr->maintFlag);
			break;
		}
	}
	fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+\n\n");
	fflush(logFile);
}

/****************************************************************************************/
/*	Function   getDutySoFar00                 -- Old function, replaced by getDutySoFar01  
/****************************************************************************************/

/****************************************************************************************/
/*	Function   getAcFlownList                -- Old function, replaced by getLastActivityLeg
/****************************************************************************************/ 
static void
getAcFlownList(PRE_Crew *preCrewPtr)
{
	int errNbr;
	LookupRet lkRet;
	int assignedToAC[MAX_ACTIVITY_LEGS];
	int idx, ix;
	SS_CrewData *ssCdPtr;
	CrewData *bwPtr;
	CS_CrewData *crwPtr;
	BINTREENODE *tmpSS, *tmpBW, *tmpCS;
	DateTime pwStartTime, flight_time, flight_time_x;

	RawAircraftData *radPtr, radBuf;
	BINTREENODE *tmp;

	pwStartTime = dt_time_tToDateTime(optParam.windowStart);

	// get list of aircrafts associated with crew member
	for(idx = 0; idx < 20; ++idx)
		assignedToAC[idx] = 0;
	idx = 0;
	for(tmpSS = Minimum(preCrewPtr->ssCrewDataRoot); tmpSS; tmpSS = Successor(tmpSS)) {
		ssCdPtr = (SS_CrewData *) getTreeObject(tmpSS);
		for(tmpBW = Minimum(ssCdPtr->bwWorkRoot); tmpBW; tmpBW = Successor(tmpBW)) {
			bwPtr = (CrewData *) getTreeObject(tmpBW);
			if(bwPtr->aircraftid)
				checkAircraftList(assignedToAC, &idx, bwPtr->aircraftid, 20);
		}
	}
	for(tmpCS = Minimum(preCrewPtr->csCrewDataRoot); tmpCS; tmpCS = Successor(tmpCS)) {
		crwPtr = (CS_CrewData *) getTreeObject(tmpCS);
		if(crwPtr->ca_aircraftid)
			checkAircraftList(assignedToAC, &idx, crwPtr->ca_aircraftid, 20);
		if(crwPtr->cd_aircraftid)
			checkAircraftList(assignedToAC, &idx, crwPtr->cd_aircraftid, 20);
	}

	if(idx == 0)
		return;

	for(ix = 0; ix < idx; ++ix) {
		// find the RawAircraftData associated with this aircraft
		memset(&radBuf, '\0', sizeof(RawAircraftData));
		radBuf.aircraftid = assignedToAC[ix];
		radBuf.rec_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", "1970/01/01 00:00", NULL, &errNbr);

		lkRet = Lookup(rawAircraftRoot, &radBuf, rawAircraftCompare, &tmp);
		switch(lkRet) {
		case EmptyTree:
		case GreaterThanLastItem:
		case ExactMatchFound: // impossible, since we don't enter other fields in key
				logMsg(logFile,"%s Line %d: aircraft %d not found.\n", __FILE__,__LINE__, radBuf.aircraftid);
				return;
		case NotFoundReturnedNextItem:
			for(; tmp; tmp = Successor(tmp)) {
				radPtr = (RawAircraftData *) getTreeObject(tmp);
				if(radPtr->aircraftid != assignedToAC[ix])
					break;
				flight_time = (radPtr->actualout) ? radPtr->actualout : radPtr->rec_outtime;
				if((strcmp(radPtr->rowtype,"logmgdleg") == 0 || strcmp(radPtr->rowtype,"mgdleg") == 0) && 
					validateCrewAircraftAssociation(preCrewPtr->employeenumber, flight_time, radPtr->aircraftid)) {
				        if(flight_time < pwStartTime && flight_time > preCrewPtr->tourStartTm)
						preCrewPtr->availAirportID = radPtr->inaptid;						
					if(!(preCrewPtr->flownRoot = RBTreeInsert(preCrewPtr->flownRoot, radPtr, flownAircraftCompare))) {
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in getAcFlownList(). crew member %s.\n",
							__FILE__,__LINE__,
							preCrewPtr->employeenumber);
						writeWarningData(myconn); exit(1);
					}
					if(OnSSday(flight_time, preCrewPtr->tourStartTm)) {
						if(preCrewPtr->firstFlightOfTour) {
							flight_time_x = (preCrewPtr->firstFlightOfTour->actualout) ?
							    preCrewPtr->firstFlightOfTour->actualout : preCrewPtr->firstFlightOfTour->rec_outtime;
							if(flight_time < flight_time_x)
								preCrewPtr->firstFlightOfTour = radPtr;
						}
						else
							preCrewPtr->firstFlightOfTour = radPtr;
					}
					if(OnSSday(flight_time, preCrewPtr->tourEndTm)) {
						if(preCrewPtr->lastFlightOfTour) {
							flight_time_x = (preCrewPtr->lastFlightOfTour->actualout) ?
							    preCrewPtr->lastFlightOfTour->actualout : preCrewPtr->lastFlightOfTour->rec_outtime;
							if(flight_time > flight_time_x)
								preCrewPtr->lastFlightOfTour = radPtr;
						}
						else
							preCrewPtr->lastFlightOfTour = radPtr;
					}
				}
			}
		}
	}
}


/****************************************************************************************************
*	Function	getDutySoFar01							Date last modified:  10/09/07 ANG
*	Purpose:	To replace function getdutySoFar00()
*   Input  :	optParam values, pre_crew.lastActivityLeg, pre_crew.tourStartTm, pre_crew.tourEndTm,
*				pre_crew.startEarly, pre_crew.stayLate, pre_crew.firstFlightOfTour, pre_crew.on1,
*				pre_crew.off1, pre_crew.on2, pre_crew.off2, pre_crew.startLoc
*	Output :	preCrew.cscd_availDT, preCrew.cscd_canStartLater, preCrew.availAirportID,
						preCrew.dutyTime, preCrew.blockTime
*	Note   :  - See documentation (Optimizer_CrewInputDataPseudocode.xls) for details
*   Assume :  - If the lastActivityLeg exists, then on1 information exists
*			  - If on1 information exists, then (pwS - on1) < 24 hours
*			  - If on2 information exists, then (on2 - pwS) < 24 hours
****************************************************************************************************/

/**********
MAJOR change on availDT to get rid of most (pws - preflight) out from the MAX. RLZ 06202008
**************/
static int
getDutySoFar01(PRE_Crew *preCrewPtr)
{
	RawAircraftData *radLastLegPtr;
	DateTime dt_pwStart, dt_runTime, flight_in_time, on2Info, off2Info;
	int tPreFltTm = 0;
	int days, hours, minutes, seconds, msecs;
	int dutytime;
	int updateLoc = 0;
	//BINTREENODE *tmp;
//	CrewData *cdptr;

	//if(! preCrewPtr->availAirportID)
	//	preCrewPtr->availAirportID = preCrewPtr->startLoc;



	dt_pwStart = dt_time_tToDateTime(optParam.windowStart);
	dt_runTime = dt_run_time_GMT;


	radLastLegPtr = preCrewPtr->lastActivityLeg;

	if(radLastLegPtr) {
		if(strcmp(radLastLegPtr->rowtype,"logmgdleg") == 0 || strcmp(radLastLegPtr->rowtype,"mgdleg") == 0){
			//the last leg is a flight leg
			if(!preCrewPtr->on1 && verbose){
				fprintf(logFile, "\n Found crew with lastActivityLeg = 'logmgdleg' or 'mgdleg' but no (ON1,OFF1) information: \n");
				fprintf(logFile, "ErrorID = 0, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
				fprintf(logFile, "\n");
			}//end if
			preCrewPtr->availAirportID = radLastLegPtr->inaptid;
			if((radLastLegPtr->rec_outtime < dt_pwStart) && (radLastLegPtr->rec_intime >= dt_pwStart)){
				//flight start time < planning window start < flight end time
				preCrewPtr->cscd_availDT = radLastLegPtr->rec_intime;
				preCrewPtr->cscd_canStartLater = 0;
				dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
				dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				preCrewPtr->dutyTime=dutytime;
				preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
			}//end if
			else if((radLastLegPtr->rec_intime < dt_pwStart) && (radLastLegPtr->rec_outtime >= dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm))){
				//means: flight in time < planning window start 
				flight_in_time = radLastLegPtr->rec_intime;
				if((preCrewPtr->on1 < dt_pwStart) && (dt_pwStart <= preCrewPtr->off1)){
					//pilot will be on duty at the start of planning window
					if(flight_in_time > preCrewPtr->on1){
						//last flight leg and planning window start are in the same duty period
						preCrewPtr->cscd_availDT = Max(flight_in_time, 
													   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));
						//preCrewPtr->cscd_availDT = Max(flight_in_time, 
						//							   Max(dt_addToDateTime(Minutes, -optParam.preFlightTm, dt_pwStart), 
						//								   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime)));
						//preCrewPtr->cscd_availDT = Max(flight_in_time, 
						//							   dt_addToDateTime(Minutes, -optParam.preFlightTm, dt_pwStart));// DEBUG
						preCrewPtr->cscd_canStartLater = 0;
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			            dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				        preCrewPtr->dutyTime=dutytime;
                        preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);

					}//end if
					else {
						//last flight leg and planning window start are in different duty periods - depends on run start time
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -optParam.firstPreFltTm, dt_pwStart), 
														Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime), 
															preCrewPtr->on1));
						preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) < 
														  dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on1)) ? 1 : 0;
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
						dutytime = (24 * 60 * days) + (60 * hours) + minutes;
						preCrewPtr->dutyTime=dutytime;
						preCrewPtr->blockTime=0;
					}//end else
				}//end if
				else {
					//pilot will be resting at the start of planning window
					preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1));
					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <  preCrewPtr->off1) ? 1 : 0;
					preCrewPtr->blockTime=0;
					preCrewPtr->dutyTime=0;
				}//end else
			}//end elseif
			else {
				//new 'Flight Leg' case - Display Warning
				if(verbose){
					fprintf(logFile, "\n Found error(s) for crew whose lastActivityLeg is a mgdleg or logmgdleg: \n");
					fprintf(logFile, "ErrorID = 1, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
					fprintf(logFile, "\n");
				}//end if
			}//end else
		}//end if
		
		else if(strcmp(radLastLegPtr->rowtype,"mtcnote") == 0 || strcmp(radLastLegPtr->rowtype,"ownersign") == 0)
		{
			if(!preCrewPtr->on1 && !preCrewPtr->on2 && verbose){
				fprintf(logFile, "\n Found crew with lastActivityLeg = 'mtcnote' or 'ownersign' but no (ON1,OFF1) and (ON2,OFF2) information: \n");
				fprintf(logFile, "ErrorID = 0, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
				fprintf(logFile, "\n");
			}//end if			//the last leg is a maintenance or an ownersigning
			preCrewPtr->availAirportID = radLastLegPtr->inaptid;
			if((radLastLegPtr->rec_outtime <= dt_pwStart) && 
				 (radLastLegPtr->rec_intime >= dt_pwStart) &&
				 (radLastLegPtr->rec_intime <= dt_addToDateTime(Minutes, optParam.maintTmForReassign, dt_pwStart)))
			{ //MX case #1A
				on2Info = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1));
				off2Info = (preCrewPtr->on2) ? (preCrewPtr->off2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.maxDutyTm, preCrewPtr->off1));
				if(((radLastLegPtr->rec_intime >= preCrewPtr->on1 && radLastLegPtr->rec_intime <= preCrewPtr->off1) || 
				   (radLastLegPtr->rec_intime >= preCrewPtr->on2 && radLastLegPtr->rec_intime <= preCrewPtr->off2)) &&
				   (radLastLegPtr->rec_intime <= dt_addToDateTime(Hours, 24*(int)preCrewPtr->stayLate, preCrewPtr->tourEndTm)))
				{ 
					//that is, if ((mx.end in (on1, off1) || mx.end in (on2, off2)) AND (mx.end < tourEndTm+stayLate))
					//pilot will be on duty at the end of MX 
					preCrewPtr->cscd_canStartLater = 0;

					tPreFltTm = (preCrewPtr->firstFlightOfTour) ?
								((preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart) ? optParam.preFlightTm : optParam.firstPreFltTm) :
								(optParam.firstPreFltTm);
			    
					if(radLastLegPtr->rec_intime >= preCrewPtr->on1 && radLastLegPtr->rec_intime <= preCrewPtr->off1)
					{ 
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -tPreFltTm, radLastLegPtr->rec_intime), preCrewPtr->on1);
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
					}
					if(radLastLegPtr->rec_intime >= preCrewPtr->on2 && radLastLegPtr->rec_intime <= preCrewPtr->off2)
					{ 
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -tPreFltTm, radLastLegPtr->rec_intime), preCrewPtr->on2);
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on2, &days, &hours, &minutes, &seconds, &msecs);
					}
					dutytime = (24 * 60 * days) + (60 * hours) + minutes;
					preCrewPtr->dutyTime=dutytime;

			        if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else 
						preCrewPtr->blockTime=0;				
				}
				else if((radLastLegPtr->rec_intime >= preCrewPtr->off1) && (radLastLegPtr->rec_intime <= on2Info) &&
						(radLastLegPtr->rec_intime <= dt_addToDateTime(Hours, 24*(int)preCrewPtr->stayLate, preCrewPtr->tourEndTm)))
				{
					//pilot will be resting at the end of MX, but end of MX is between off1 and on2
					preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1));
					//Depending on pwS, we might or might not need to consider run start time
					if(dt_pwStart <= preCrewPtr->off1){
						//Don't need to consider run start time
						preCrewPtr->cscd_canStartLater = 1;
					}//end if
					else {
						//Do need to consider run start time
						preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1) ? 1 : 0;
					}//end else
					preCrewPtr->blockTime=0;
					preCrewPtr->dutyTime=0;
				}//end else
				else if((radLastLegPtr->rec_intime >= off2Info)&&
						(radLastLegPtr->rec_intime < dt_addToDateTime(Hours, 24*(int)preCrewPtr->stayLate, preCrewPtr->tourEndTm)))
				{
					//pilot will be resting at the end of MX, but end of MX is bigger than off2
					//This is not suppose to happen (or at least, very rarely)
					//Here we do NOT need to consider run start time - always have enough time to notify
					preCrewPtr->cscd_canStartLater = 1;
					preCrewPtr->cscd_availDT = dt_addToDateTime(Minutes, optParam.minRestTm, off2Info);
                    preCrewPtr->blockTime=0;
					preCrewPtr->dutyTime=0;
				}//end elseif
				else 
					preCrewPtr->availDuringPlanningWindow=0;
			}//end if MX case #1A

			else if((radLastLegPtr->rec_outtime <= dt_pwStart) && 
				    (radLastLegPtr->rec_intime >= dt_pwStart) &&
				    (radLastLegPtr->rec_intime > dt_addToDateTime(Minutes, optParam.maintTmForReassign, dt_pwStart)))
                { //MX case #1B 
				if((preCrewPtr->on1 <= dt_pwStart) &&
				   (preCrewPtr->off1 >= dt_pwStart))
				{
					//pilot will be on duty at the start of planning window
					preCrewPtr->cscd_canStartLater = 0;

					tPreFltTm = (preCrewPtr->firstFlightOfTour) ?
								((preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart) ? optParam.preFlightTm : optParam.firstPreFltTm) :
								(optParam.firstPreFltTm);

					//preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
					//							   dt_addToDateTime(Minutes, -tPreFltTm, dt_pwStart));
//RLZ replaced the above 06202008
					preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
										 dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));
		
						                       
				    dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			        dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				    preCrewPtr->dutyTime=dutytime;

			        if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else 
						preCrewPtr->blockTime=0;
				}//end if
				else 
				{
					//pilot will be resting at the start of planning window
					preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.minTimeToNotify, preCrewPtr->off1));
					//Do need to consider run start time to determine canStartLater	
					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1) ? 1 : 0;
					preCrewPtr->dutyTime=0;
			        if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else 
						preCrewPtr->blockTime=0;
				}//end else
			}//end elseif MX case #1B
			else if(radLastLegPtr->rec_intime < dt_pwStart)
			{ //MX case #2
				tPreFltTm = (preCrewPtr->firstFlightOfTour) ?
							((preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart) ? optParam.preFlightTm : optParam.firstPreFltTm) :
							(optParam.firstPreFltTm);

				if((preCrewPtr->on1 <= dt_pwStart)&&(preCrewPtr->off1 >= dt_pwStart))
				{
					//pilot will be on duty at the start of planning window
					//preCrewPtr->cscd_canStartLater = 0;
				//	preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
				//								   dt_addToDateTime(Minutes, -tPreFltTm, dt_pwStart)); 

					preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
						dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));


					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) < 
									dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on1)) ? 1 : 0;
				    dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			        dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				    preCrewPtr->dutyTime=dutytime;
					if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else
						preCrewPtr->blockTime=0;						   
				}//end if
				else {
					//pilot will be resting at the start of planning window
					//Do need to consider run start time
					if(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1){
						preCrewPtr->cscd_canStartLater = 1;
						preCrewPtr->cscd_availDT = dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1);
						preCrewPtr->dutyTime=0;
						preCrewPtr->blockTime=0;
					}//end if
					else{
						preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.minTimeToNotify, preCrewPtr->off1));
						preCrewPtr->cscd_canStartLater = (preCrewPtr->on2) ? 0 : 1;
						preCrewPtr->dutyTime=0;
						preCrewPtr->blockTime=0;
					}//end else																						
				}//end else
			}//end elseif MX case #2
			else 
			{
				//new MX case - Display Warning
				if(verbose)
				{
					fprintf(logFile, "\n Found error(s) for crew whose lastActivityLeg is a 'mtcnote' or 'ownersign': \n");
					fprintf(logFile, "ErrorID = 2, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
					fprintf(logFile, "\n");
				}//end if
			}//end else
		}//end else if
		
		else {
			//rowtype is other than logmgdleg, mgdleg, mcnote, ownersign - Display Warning
			if(verbose){
				fprintf(logFile, "\n Found error for crew whose lastActivityLeg is not one of these: 'mgdleg', 'logmgdleg', 'mtcnote', 'ownersign' \n");
				fprintf(logFile, "ErrorID = 3, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
				fprintf(logFile, "\n");
			}//end if
			return(0);
		}
		
		//lastly: check if pilot is available during the planning window
		if (dt_addToDateTime(Minutes, optParam.finalPostFltTm, preCrewPtr->cscd_availDT) > dt_addToDateTime(Hours, 24.*preCrewPtr->stayLate, preCrewPtr->tourEndTm)){
			preCrewPtr->availDuringPlanningWindow = 0;
			return(0);
		}
	}//end if radLastLegPtr
	else 
	{
		//there is no lastLeg information from lastActivityLeg
		if(preCrewPtr->firstFlightOfTour && preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart )
		{
			//lastActivityLeg is not identified but firstFlightOfTour is defined - Error, Display Warning
			if(verbose)
			{
				fprintf(logFile, "\n Found crew with lastActivityLeg but no firstFlightOfTour: \n");
				fprintf(logFile, "ErrorID = 4, CrewID = %s, lastActivityLeg is not identified but firstFlightOfTour is defined", preCrewPtr->employeenumber);
				fprintf(logFile, "\n");
			}//end if
		}//end if 
		else{
			//this is the 'tour start' case
			preCrewPtr->availAirportID = preCrewPtr->startLoc;
			updateLoc = 1;
			if((preCrewPtr->on1) && (dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm) <= dt_pwStart)){  
				//pilot has started tour at the start of planning window and on1 information is available
				if(preCrewPtr->on1 <= dt_pwStart && dt_pwStart <= preCrewPtr->off1){
					//pilot will be on duty at the start of planning window
					preCrewPtr->cscd_canStartLater = 0;
					preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime),
						                           dt_addToDateTime(Minutes, -optParam.firstPreFltTm, dt_pwStart)));
					dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			        dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				    preCrewPtr->dutyTime=dutytime;
					preCrewPtr->blockTime=0;
				    
				}//end if
				else {
					//pilot will be resting at the start of planning window
					//Do need to consider run start time
					if(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1){
						preCrewPtr->cscd_canStartLater = 1;
						preCrewPtr->cscd_availDT = dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1);
					}//end if
					else{
						preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.minTimeToNotify, preCrewPtr->off1));
						preCrewPtr->cscd_canStartLater = (preCrewPtr->on2) ? 0 : 1;
					}//end else																											
					preCrewPtr->dutyTime=0;
					preCrewPtr->blockTime=0;
				}//end else
			}//end if
			else if((dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm) <= dt_pwStart)){  
				//pilot has started tour at the start of planning window but on1 information is not available
				preCrewPtr->cscd_canStartLater = 1;
				preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, optParam.minTmToTourStart, dt_runTime),
				                           Max(dt_addToDateTime(Minutes, -optParam.firstPreFltTm, dt_pwStart),
										       dt_addToDateTime(Minutes, -24*60*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm)));
				preCrewPtr->dutyTime=0;
				preCrewPtr->blockTime=0;				    				
			}//end elseif
			else {
				//pilot has not started tour at the start of planning window (therefore on1 is not available)
				preCrewPtr->cscd_canStartLater = 1;
				if(preCrewPtr->on2)
				{
					preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm),
											       Min(dt_addToDateTime(Minutes, optParam.minTmToTourStart, dt_runTime),
												       preCrewPtr->on2));
				}//end if
				else 
				{
					preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm),
												   dt_addToDateTime(Minutes, optParam.minTmToTourStart, dt_runTime));
				}//end else
				preCrewPtr->dutyTime=0;//need to change, later
				preCrewPtr->blockTime=0;
			}//end else
		}//end else
	}//end else radLastLegPtr  

	if ( preCrewPtr->on2 && optParam.estimateCrewLoc && updateLoc && preCrewPtr->assignedACID && preCrewPtr->AC_AirportID_beforePW && dt_addToDateTime(Hours, -12 - 24.*preCrewPtr->startEarly, preCrewPtr->tourStartTm) < dt_pwStart) 
		//This crew has crewassignment in BW, we update its location same as the AC. RLZ 01/15/2008
         preCrewPtr->availAirportID = preCrewPtr->AC_AirportID_beforePW;
	if(! preCrewPtr->availAirportID)
		preCrewPtr->availAirportID = preCrewPtr->startLoc;

	return(0);
}//end of function


/****************************************************************************************************
*	Function	getDutySoFarAlt							Date last modified:  10/29/07 ANG 04/09/09 ANG
*	Purpose:	Extension to function getdutySoFar01() to get alternate availDT (if possible)
*	Output :	preCrew.cscd_availDT modified (if there exists earlier availDT)
*	Notes  :	Only applies to crew that:
*				- is available during the planning window
*				- is resting at the start of the planning window
****************************************************************************************************/

static int
getDutySoFarAlt(PRE_Crew *preCrewPtr)
{
	RawAircraftData *radPtr;//, *radPtr2;
	DateTime alt_availDT, dt_pwStart, dt_runTime, dt_newOffTime, dt_minDutyStart;
	DateTimeParts dtparts;
	CsTravelData *cstrlPtr;
	char dbuf1[32], dbuf2[32];
	DateTime cstrl_arrTm, cstrl_dptTm;
	int cstrl_ignored = 0;

	dt_pwStart = dt_time_tToDateTime(optParam.windowStart);
	dt_runTime = dt_run_time_GMT;

	radPtr = preCrewPtr->lastActivityLeg;
	cstrlPtr = preCrewPtr->lastCsTravelLeg;
	//radPtr2	= preCrewPtr->lastActivityLeg2;

	if(cstrlPtr)
	{ cstrl_arrTm = dt_addToDateTime(Minutes, optParam.postArrivalTime, cstrlPtr->travel_arrTm);
	  cstrl_dptTm = dt_addToDateTime(Minutes, -optParam.preBoardTime, cstrlPtr->travel_dptTm);
	}
	else
      cstrl_arrTm = 0;

	if(radPtr && cstrlPtr){  
		//Travel after pws and after a leg, ignore travel
		if(strcmp(radPtr->rowtype,"logmgdleg") == 0 || strcmp(radPtr->rowtype,"mgdleg") == 0){
			if((cstrl_arrTm >= radPtr->rec_intime) && (cstrl_dptTm > dt_pwStart)){
				//cstrlPtr = NULL;
				cstrl_ignored = 1; 
			}
		}
		//Travel after pws and  lastActLeg is short MX, ignore travel
		if(strcmp(radPtr->rowtype,"mtcnote") == 0 || strcmp(radPtr->rowtype,"ownersign") == 0){
			if(cstrl_dptTm > dt_pwStart &&
				(radPtr->rec_intime <= dt_addToDateTime(Minutes, optParam.maintTmForReassign, dt_pwStart))){
			//	cstrlPtr = NULL;
				cstrl_ignored = 1;
			}
		}	
	}

	//if(preCrewPtr->availDuringPlanningWindow != 0 && (!cstrlPtr || ){
	//RLZ: we identify some exceptions before coming into this function, related to Travel data.
	//ANG: Exceptions has been removed before coming into this function in (getCrewList())
	if(preCrewPtr->availDuringPlanningWindow != 0){
		//Logic in determining whether crew can be available earlier:
		//IF last thing done by the crew is an radPtr (regardless whether cstrlPtr exists), then adjust time if pilot is resting at pws
		//ELSE IF last thing done by the crew is a cstrlPtr (regardless whether radPtr exists), then adjust time if pilot is resting at pws
		//ELSE this is when none of radPtr nor cstrlPtr exists

		if((radPtr && !cstrlPtr) || (radPtr && cstrlPtr && cstrl_ignored == 1) || (radPtr && cstrlPtr && cstrl_ignored == 0 && radPtr->rec_outtime > cstrl_arrTm)){
		//if(radPtr){
			//radPtr is defined and so is radPtr2
			if(strcmp(radPtr->rowtype,"logmgdleg") == 0	|| strcmp(radPtr->rowtype,"mgdleg")	== 0){
				//the last leg is a	flight leg
				if((radPtr->rec_intime < dt_pwStart) &&	(radPtr->rec_outtime >=	dt_addToDateTime(Hours,	-24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm))){
					//means: flight	in time	< planning window start	
					if(!((preCrewPtr->on1 <	dt_pwStart)	&& (dt_pwStart <= preCrewPtr->off1))){
						//pilot	will be	resting	at the start of	planning window
						dt_newOffTime =	Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify,	dt_runTime),
											dt_addToDateTime(Minutes, optParam.postFlightTm, radPtr->rec_intime));
						alt_availDT	= Min(dt_addToDateTime(Minutes,	optParam.minRestTm,	dt_newOffTime),	preCrewPtr->cscd_availDT);

						//Compare alt_availDT one more time	with 4AM local time
						dt_DateTimeToDateTimeParts(alt_availDT, &dtparts);
						dt_minDutyStart = dt_MDYHMSMtoDateTime((int) dtparts.dparts.month, (int) dtparts.dparts.day, (int) dtparts.dparts.year, optParam.dutyStartFromMidnight/60, 0, 0, 0);
						dt_minDutyStart = dt_addToDateTime(Minutes, locFromGMTinMinutes + timeZoneAdjByApt(preCrewPtr->availAirportID, dt_minDutyStart)/60, dt_minDutyStart); //LOCAL
						if(dt_minDutyStart > alt_availDT) { alt_availDT = dt_minDutyStart; }
	
						//Print	out	message
						if(preCrewPtr->cscd_availDT > alt_availDT){
							if(verbose)	{
								fprintf(logFile, "Earlier availDT is defined for crewID	= %d in getDutySofarAlt, was: %s, now: %s \n", preCrewPtr->crewID, dt_DateTimeToDateTimeString(preCrewPtr->cscd_availDT, dbuf1, "%Y/%m/%d %H:%M"), dt_DateTimeToDateTimeString(alt_availDT, dbuf2, "%Y/%m/%d %H:%M"));
							}
							preCrewPtr->cscd_availDT = alt_availDT;
							preCrewPtr->cscd_canStartLater = 1;
						}

					}//end if
				}//end if
			}//end if		
		}//end if
		else if (cstrl_ignored == 0 && ((!radPtr && cstrlPtr) || (radPtr && cstrlPtr && radPtr->rec_intime < cstrl_dptTm ))){
			if(cstrlPtr->arr_aptID == preCrewPtr->availAirportID){
				//the last leg is a	commercial flight leg
				if((cstrlPtr->travel_arrTm < dt_pwStart) &&	(cstrlPtr->travel_dptTm >= dt_addToDateTime(Hours,	-24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm))){
					//means: travel arrival time < planning window start
					if(!((preCrewPtr->on1 <	dt_pwStart)	&& (dt_pwStart <= preCrewPtr->off1))){
						//pilot	will be	resting	at the start of	planning window
						dt_newOffTime =	Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify,	dt_runTime),
											dt_addToDateTime(Minutes, optParam.postArrivalTime, cstrlPtr->travel_arrTm));
						alt_availDT	= Min(dt_addToDateTime(Minutes,	optParam.minRestTm,	dt_newOffTime),	preCrewPtr->cscd_availDT);

						//Compare alt_availDT one more time	with 4AM local time
						dt_DateTimeToDateTimeParts(alt_availDT, &dtparts);
						dt_minDutyStart = dt_MDYHMSMtoDateTime((int) dtparts.dparts.month, (int) dtparts.dparts.day, (int) dtparts.dparts.year, optParam.dutyStartFromMidnight/60, 0, 0, 0);
						dt_minDutyStart = dt_addToDateTime(Minutes, locFromGMTinMinutes + timeZoneAdjByApt(preCrewPtr->availAirportID, dt_minDutyStart)/60, dt_minDutyStart); //LOCAL
						if(dt_minDutyStart > alt_availDT) { alt_availDT = dt_minDutyStart; }
	
						//Print	out	message
						if(preCrewPtr->cscd_availDT > alt_availDT){
							if(verbose)	{
								fprintf(logFile, "Earlier availDT is defined for crewID	= %d in getDutySofarAlt, was: %s, now: %s \n", preCrewPtr->crewID, dt_DateTimeToDateTimeString(preCrewPtr->cscd_availDT, dbuf1, "%Y/%m/%d %H:%M"), dt_DateTimeToDateTimeString(alt_availDT, dbuf2, "%Y/%m/%d %H:%M"));
							}
							preCrewPtr->cscd_availDT = alt_availDT;
							preCrewPtr->cscd_canStartLater = 1;
						}

					}//end if
				}//end if
			}//end if	
		}//end else if
		else {
			//if(!((preCrewPtr->on1 <	dt_pwStart)	&& (dt_pwStart <= preCrewPtr->off1))){
			if(dt_runTime > dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm)){
				//pilot	will be	resting	at the start of	planning window
				//RLZ notes: we basically want to get the availDT as early as possible. the later piece will determine whether to use the OT.
				dt_newOffTime =	dt_addToDateTime(Minutes, optParam.minTimeToNotify,	dt_runTime);
				alt_availDT	= Min(dt_addToDateTime(Minutes,	optParam.minRestTm,	dt_newOffTime),	preCrewPtr->cscd_availDT);

				//Compare alt_availDT one more time	with 4AM local time
				dt_DateTimeToDateTimeParts(alt_availDT, &dtparts);
				dt_minDutyStart = dt_MDYHMSMtoDateTime((int) dtparts.dparts.month, (int) dtparts.dparts.day, (int) dtparts.dparts.year, optParam.dutyStartFromMidnight/60, 0, 0, 0);
				dt_minDutyStart = dt_addToDateTime(Minutes, locFromGMTinMinutes + timeZoneAdjByApt(preCrewPtr->availAirportID, dt_minDutyStart)/60, dt_minDutyStart); //LOCAL
				if(dt_minDutyStart > alt_availDT) { alt_availDT = dt_minDutyStart; }
	
				//Print	out	message
				if(preCrewPtr->cscd_availDT > alt_availDT){
					if(verbose)	{
						fprintf(logFile, "Earlier availDT is defined for crewID	= %d in getDutySofarAlt, was: %s, now: %s \n", preCrewPtr->crewID, dt_DateTimeToDateTimeString(preCrewPtr->cscd_availDT, dbuf1, "%Y/%m/%d %H:%M"), dt_DateTimeToDateTimeString(alt_availDT, dbuf2, "%Y/%m/%d %H:%M"));
					}
					preCrewPtr->cscd_availDT = alt_availDT;
					preCrewPtr->cscd_canStartLater = 1;
				}
			}//end if
		}//end else
	}//end if

	return(0);
}//end of function


/****************************************************************************************/
/*	Function   getLastActivityLeg                  Date last modified:   10/03/07 Jintao
*	Purpose:  get last activity leg before or at the start of planning window
    for a specific crew.									*
*/
/****************************************************************************************/
static void
getLastActivityLeg(PRE_Crew *preCrewPtr)
{
	int errNbr;
	LookupRet lkRet;
	int assignedToAC[MAX_ACTIVITY_LEGS];
	int idx, ix;
	CrewData *bwPtr;
	BINTREENODE *tmp;	//, *succ_tmp,*prev_tmp;
	DateTime pwStartTime, flight_outtime, flight_intime, flight_time_x;
	DateTime tourStartTmOT, tourEndTmOT;
	//DateTime succ_flight_outtime, succ_flight_intime;
    RawAircraftData *radPtr, radBuf; //, *radPtr1;
	int prevActivityLegFlag=0;
	

	pwStartTime = dt_time_tToDateTime(optParam.windowStart);
	preCrewPtr->lastActivityLeg = NULL;

	tourStartTmOT = dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm);
	tourEndTmOT = dt_addToDateTime(Hours, 24*(int)preCrewPtr->stayLate, preCrewPtr->tourEndTm);

	preCrewPtr->AC_AirportID_beforePW = 0;
	preCrewPtr->assignedACID = 0;

	// get list of aircrafts associated with crew member
	for(idx = 0; idx < MAX_ACTIVITY_LEGS; ++idx)
		assignedToAC[idx] = 0;
	idx = 0;
	
	for(tmp = Minimum(preCrewPtr->bwCrewAssgRoot); tmp; tmp = Successor(tmp)) 
	{
		bwPtr = (CrewData *) getTreeObject(tmp);
		if(bwPtr->aircraftid)
			checkAircraftList(assignedToAC, &idx, bwPtr->aircraftid, MAX_ACTIVITY_LEGS);
	}

	if(idx == 0)
		return;
	
    if (idx == 1) // Consider to update the availLoc for crew, only if there is one assigned aircraft to the crew
	    preCrewPtr->assignedACID = assignedToAC[0];

	for(ix = 0; ix < idx; ++ix) {
		// find the RawAircraftData associated with this aircraft
		memset(&radBuf, '\0', sizeof(RawAircraftData));
		radBuf.aircraftid = assignedToAC[ix];
		radBuf.rec_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", "1970/01/01 00:00", NULL, &errNbr);

		lkRet = Lookup(rawAircraftRoot, &radBuf, rawAircraftCompare, &tmp);
		switch(lkRet) {
		case EmptyTree:
		case GreaterThanLastItem:
		case ExactMatchFound: // impossible, since we don't enter other fields in key
				logMsg(logFile,"%s Line %d: aircraft %d not found.\n", __FILE__,__LINE__, radBuf.aircraftid);
				return;
		case NotFoundReturnedNextItem:
			for(; tmp; tmp = Successor(tmp)) 
			{
				radPtr = (RawAircraftData *) getTreeObject(tmp);
				//radPtr1= (RawAircraftData *) calloc((size_t) 1, sizeof(RawAircraftData));
				//memcpy(radPtr1, radPtr, sizeof(RawAircraftData));
				if(radPtr->aircraftid != assignedToAC[ix])
					break;
				if(strcmp(radPtr->rowtype,"aircraft") != 0) 
				{
					flight_outtime=radPtr->actualout?radPtr->actualout:radPtr->rec_outtime;
					flight_intime=radPtr->actualin?radPtr->actualin:radPtr->rec_intime;
			       /*else if(strcmp(radPtr->rowtype,"logmgdleg") == 0) 
				   {
				        if(radPtr->actualoff)
						{ flight_outtime=radPtr->actualoff;
						}
				        else if(radPtr->actualout) 
						{ flight_outtime = dt_addToDateTime(Minutes, optParam.taxiOutTm, radPtr->actualout);
				        }
				        else if(radPtr->rec_outtime)
					      flight_outtime=radPtr->rec_outtime;;
				        else
					      logMsg(logFile,"%Line %d, Bad logpageleg record.\n", __FILE, __LINE__);
						if(radPtr->actualin)
						{ flight_intime=radPtr->actualin;
						}
				        else if(radPtr->actualon) 
						{ flight_intime = dt_addToDateTime(Minutes, optParam.taxiInTm, radPtr->actualon);
				        }
				        else if(radPtr->rec_intime)
					      flight_intime=radPtr->rec_intime;;
				        else
					      logMsg(logFile,"%Line %d, Bad logpageleg record.\n", __FILE, __LINE__);
				   }*/
			//        radPtr1->rec_outtime=flight_outtime;
			    //    radPtr1->rec_intime=flight_intime;
					//RLZ 01/17/2008 changes for MX/Ownersigning
                   // if(validateCrewAircraftAssociation(preCrewPtr->employeenumber, flight_outtime, radPtr->aircraftid)
					//	|| validateCrewAircraftAssociationMX(preCrewPtr->employeenumber, flight_outtime, flight_intime, radPtr->aircraftid, radPtr->rowtype))
			        //RLZ 01/22/2008 adjustment for flight leg
					if(validateCrewAircraftAssociationFlightLeg(preCrewPtr->employeenumber, flight_outtime, flight_intime, radPtr->aircraftid, radPtr->rowtype)
						|| validateCrewAircraftAssociationMX(preCrewPtr->employeenumber, flight_outtime, flight_intime, radPtr->aircraftid, radPtr->rowtype))
			        { if(!(preCrewPtr->activityLegRoot = RBTreeInsert(preCrewPtr->activityLegRoot, radPtr, flownAircraftCompare))) 
						{
						logMsg(logFile,"%s Line %d, RBTreeInsert() failed in getLastActivityLeg(). crew member %s.\n",
							__FILE__,__LINE__,
							preCrewPtr->employeenumber);
						writeWarningData(myconn); exit(1);
						}
						//Roger's changes for lastActivityLeg
						//if((strcmp(radPtr->rowtype,"logmgdleg") == 0 || strcmp(radPtr->rowtype,"mgdleg") == 0)) 
					//		{
						//if(flight_outtime < pwStartTime && flight_outtime > tourStartTmOT) 
							//RLZ: put the overtime into preCrewPtr->tourStartTm 10/30/2007
							
						if(flight_outtime < pwStartTime && flight_intime > tourStartTmOT) 
							//RLZ: Last activity leg for long MX. 01/15/2008. 
						{
						   if (!preCrewPtr->lastActivityLeg)
							   preCrewPtr->lastActivityLeg = radPtr;
						   else if (preCrewPtr->lastActivityLeg->rec_outtime < radPtr->rec_outtime)
								preCrewPtr->lastActivityLeg = radPtr;
						}		


						if((strcmp(radPtr->rowtype,"logmgdleg") == 0 || strcmp(radPtr->rowtype,"mgdleg") == 0)) 
							{
								if(flight_outtime < pwStartTime && flight_outtime > tourStartTmOT) //preCrewPtr->tourStartTm)
								preCrewPtr->availAirportID = radPtr->inaptid;	
								//This is questionable since aircraft may not be processed in ascending order. Time is required.
								//But we correct this location in getDutySoFar function.					
								
								if(OnSSday(flight_outtime, tourStartTmOT)) // preCrewPtr->tourStartTm)) 
								{
								if(preCrewPtr->firstFlightOfTour) 
								{
									flight_time_x = (preCrewPtr->firstFlightOfTour->actualout) ?
										preCrewPtr->firstFlightOfTour->actualout : preCrewPtr->firstFlightOfTour->rec_outtime;
									if(flight_outtime < flight_time_x)
										preCrewPtr->firstFlightOfTour = radPtr;
								}
								else
									preCrewPtr->firstFlightOfTour = radPtr;
								}
								if(OnSSday(flight_outtime, tourEndTmOT)) //preCrewPtr->tourEndTm)) 
								{
								if(preCrewPtr->lastFlightOfTour) 
								{
									flight_time_x = (preCrewPtr->lastFlightOfTour->actualout) ?
										preCrewPtr->lastFlightOfTour->actualout : preCrewPtr->lastFlightOfTour->rec_outtime;
									if(flight_outtime > flight_time_x)
										preCrewPtr->lastFlightOfTour = radPtr;
								}
								else
									preCrewPtr->lastFlightOfTour = radPtr;
								}
							}
				  }
					if(flight_outtime < pwStartTime){ // && flight_intime > tourStartTmOT) { 
						//Track the location of the aircraft. To be used to update availLoc for assigned crew later on
						// RLZ 01/15/2008
						preCrewPtr->AC_AirportID_beforePW = radPtr->inaptid;
					}
				}
			}
		}
	}

/*
    if(preCrewPtr->activityLegRoot)
	{ for(tmp=Minimum(preCrewPtr->activityLegRoot);tmp;tmp=Successor(tmp))
	 { radPtr=(RawAircraftData *) getTreeObject(tmp);
         flight_outtime=radPtr->actualout?radPtr->actualout:radPtr->rec_outtime;
         flight_intime=radPtr->actualin?radPtr->actualin:radPtr->rec_intime;
	  
         if(succ_tmp=Successor(tmp))
		 { radPtr1=(RawAircraftData *) getTreeObject(succ_tmp);
		   succ_flight_outtime=(radPtr1->actualout?radPtr1->actualout:radPtr1->rec_outtime);
		   succ_flight_intime=(radPtr1->actualin?radPtr1->actualin:radPtr1->rec_intime);
		 }
	     else
		 { succ_flight_outtime=0;
		   succ_flight_intime=0;
		 }
	  
	  if(flight_outtime<pwStartDate && flight_intime<=pwStartDate && succ_flight_outtime<pwStartDate)
	   { prevActivityLegFlag++;
	     prev_tmp = tmp;
		 continue;
	   }
	  else if(flight_outtime>=pwStartDate)
		    { preCrewPtr->lastActivityLeg=NULL;
		      break;
	        }
	  else
	        { memcpy(preCrewPtr->lastActivityLeg,radPtr,sizeof(RawAircraftData));
		      break;
	        }
	       
	}
	if(!tmp)
	{ memcpy(preCrewPtr->lastActivityLeg,radPtr,sizeof(RawAircraftData));
	}
  }
	else
    preCrewPtr->lastActivityLeg=NULL;
*/

}


static int
validateCrewAircraftAssociation(char *empnbr, DateTime outtime, int aircraftid)
{
	BINTREENODE *tmp;
	LookupRet lkRet;
	CrewData crwBuf, *crwPtr;
	char tbuf[32];

	memset(&crwBuf, '\0', sizeof(CrewData));
	crwBuf.employeenumber = tbuf;
	strcpy(crwBuf.employeenumber, empnbr);
	lkRet = Lookup(empnbrStarttimeActypRegRoot, &crwBuf, empnbrStarttimeActypRegCmp, &tmp);
	switch(lkRet) {
	case ExactMatchFound: // impossible, since we don't enter other fields in key
			logMsg(logFile,"%s Line %d: Lookup() employeenumber %s. lkRet = %s\n", __FILE__,__LINE__, empnbr, bintreeRet[lkRet]);
			return(0);
	case NotFoundReturnedNextItem:
		for(; tmp; tmp = Successor(tmp)) {
			crwPtr = (CrewData *) getTreeObject(tmp);
			if(strcmp(crwPtr->employeenumber, empnbr) == 0) {
				if(crwPtr->aircraftid == aircraftid) {
					if(outtime >= crwPtr->starttime && outtime <= crwPtr->endtime)
						return(1);
				}
			}
			else
				return(0);

		}
		return(0);
	case EmptyTree:
	case GreaterThanLastItem:
	default:
		return(0);
	}
	return(0);
}

/*
RLZ 01/17/2008, To check if a mx/owersigning can be considered as activity by crew. criteria: overlap any part of duty time.
*/

static int
validateCrewAircraftAssociationMX(char *empnbr, DateTime outtime, DateTime intime, int aircraftid, char *rowType)
{
	BINTREENODE *tmp;
	LookupRet lkRet;
	CrewData crwBuf, *crwPtr;
	char tbuf[32];
	
	if ( strcmp(rowType,"mtcnote") != 0 && strcmp(rowType,"ownersign") != 0)
		return (0);
	memset(&crwBuf, '\0', sizeof(CrewData));
	crwBuf.employeenumber = tbuf;
	strcpy(crwBuf.employeenumber, empnbr);
	lkRet = Lookup(empnbrStarttimeActypRegRoot, &crwBuf, empnbrStarttimeActypRegCmp, &tmp);
	switch(lkRet) {
	case ExactMatchFound: // impossible, since we don't enter other fields in key
			logMsg(logFile,"%s Line %d: Lookup() employeenumber %s. lkRet = %s\n", __FILE__,__LINE__, empnbr, bintreeRet[lkRet]);
			return(0);
	case NotFoundReturnedNextItem:
		for(; tmp; tmp = Successor(tmp)) {
			crwPtr = (CrewData *) getTreeObject(tmp);
			if(strcmp(crwPtr->employeenumber, empnbr) == 0) {
				if(crwPtr->aircraftid == aircraftid) {
					//if(intime >= crwPtr->starttime && outtime <= crwPtr->endtime) //Intersect 
					//		return(1);

					if (optParam.withCTC) {
						//with CTC, we can ignore some MX as the last activity leg RLZ 09/18/2008
						if(outtime >= crwPtr->starttime && intime <= crwPtr->endtime) //cover not intersect
							return(1);
						if(outtime <= crwPtr->starttime && intime >= crwPtr->endtime) //MX completely cover the crew duty line.
							return(1);

					}
					else {
					// the old case prior the ctc input: RLZ 09/18/2008	
						if(intime >= crwPtr->starttime && outtime <= crwPtr->endtime) //Intersect 
							return(1);
					}

				}
			}
			else
				return(0);

		}
		return(0);
	case EmptyTree:
	case GreaterThanLastItem:
	default:
		return(0);
	}
	return(0);
}

/*
RLZ 01/17/2008, To check if a flight leg can be considered as activity by crew. criteria: duty time covers out and in time.
*/

static int
validateCrewAircraftAssociationFlightLeg(char *empnbr, DateTime outtime, DateTime intime, int aircraftid, char *rowType)
{
	BINTREENODE *tmp;
	LookupRet lkRet;
	CrewData crwBuf, *crwPtr;
	char tbuf[32];
	
	if ( strcmp(rowType,"logmgdleg") != 0 && strcmp(rowType,"mgdleg") != 0)
		return (0);
	memset(&crwBuf, '\0', sizeof(CrewData));
	crwBuf.employeenumber = tbuf;
	strcpy(crwBuf.employeenumber, empnbr);
	lkRet = Lookup(empnbrStarttimeActypRegRoot, &crwBuf, empnbrStarttimeActypRegCmp, &tmp);
	switch(lkRet) {
	case ExactMatchFound: // impossible, since we don't enter other fields in key
			logMsg(logFile,"%s Line %d: Lookup() employeenumber %s. lkRet = %s\n", __FILE__,__LINE__, empnbr, bintreeRet[lkRet]);
			return(0);
	case NotFoundReturnedNextItem:
		for(; tmp; tmp = Successor(tmp)) {
			crwPtr = (CrewData *) getTreeObject(tmp);
			if(strcmp(crwPtr->employeenumber, empnbr) == 0) {
				if(crwPtr->aircraftid == aircraftid) {
					if(outtime >= crwPtr->starttime && intime <= crwPtr->endtime) //cover not intersect
						return(1);
				}
			}
			else
				return(0);

		}
		return(0);
	case EmptyTree:
	case GreaterThanLastItem:
	default:
		return(0);
	}
	return(0);
}



// see if there is a leg flown by this crew member in RawAircraftData before planning window start
static int
hasAlreadyFlown(PRE_Crew *preCrewPtr)
{
	int idx, ix, errNbr;
	int assignedToAC[20];
	BINTREENODE *tmp, *tmpSS, *tmpBW, *tmpCS;
	SS_CrewData *ssCdPtr;
	CrewData *bwPtr;
	LookupRet lkRet;
	RawAircraftData *radPtr, radBuf;
	DateTime pwStartTime;
	CS_CrewData *crwPtr;
	DateTime flight_time;

	pwStartTime = dt_time_tToDateTime(optParam.windowStart);

	// get list of aircrafts associated with crew member
	for(idx = 0; idx < 20; ++idx)
		assignedToAC[idx] = 0;
	idx = 0;
	for(tmpSS = Minimum(preCrewPtr->ssCrewDataRoot); tmpSS; tmpSS = Successor(tmpSS)) {
		ssCdPtr = (SS_CrewData *) getTreeObject(tmpSS);
		for(tmpBW = Minimum(ssCdPtr->bwWorkRoot); tmpBW; tmpBW = Successor(tmpBW)) {
			bwPtr = (CrewData *) getTreeObject(tmpBW);
			if(bwPtr->aircraftid)
				checkAircraftList(assignedToAC, &idx, bwPtr->aircraftid, 20);
		}
	}
	for(tmpCS = Minimum(preCrewPtr->csCrewDataRoot); tmpCS; tmpCS = Successor(tmpCS)) {
		crwPtr = (CS_CrewData *) getTreeObject(tmpCS);
		if(crwPtr->ca_aircraftid)
			checkAircraftList(assignedToAC, &idx, crwPtr->ca_aircraftid, 20);
		if(crwPtr->cd_aircraftid)
			checkAircraftList(assignedToAC, &idx, crwPtr->cd_aircraftid, 20);
	}

	for(ix = 0; ix < idx; ++ix) {
		memset(&radBuf, '\0', sizeof(RawAircraftData));
		radBuf.aircraftid = assignedToAC[ix];
		radBuf.rec_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", "1970/01/01 00:00", NULL, &errNbr);

		lkRet = Lookup(rawAircraftRoot, &radBuf, rawAircraftCompare, &tmp);
		switch(lkRet) {
		case EmptyTree:
		case GreaterThanLastItem:
		case ExactMatchFound: // impossible, since we don't enter other fields in key
				logMsg(logFile,"%s Line %d: aircraft %d not found.\n", __FILE__,__LINE__, radBuf.aircraftid);
				return(0);
		case NotFoundReturnedNextItem:
			for(; tmp; tmp = Successor(tmp)) {
				radPtr = (RawAircraftData *) getTreeObject(tmp);
				if(radPtr->aircraftid != assignedToAC[ix])
					break;
				flight_time = (radPtr->actualout) ? radPtr->actualout : radPtr->rec_outtime;
				if((strcmp(radPtr->rowtype,"logmgdleg") == 0 || strcmp(radPtr->rowtype,"mgdleg") == 0) && (flight_time < pwStartTime )
						&& flight_time > preCrewPtr->tourStartTm) {
					if(validateCrewAircraftAssociation(preCrewPtr->employeenumber,
					    (radPtr->actualout) ? radPtr->actualout : radPtr->rec_outtime, radPtr->aircraftid))
						return(1);
				}

			}
		}
	}
	return(0);
}





// see if there is a leg flown by this crew member for given aircraft in RawAircraftData before planning window start
// search crewidRoot to get empoyeenumber
static int
hasAlreadyFlownThisPlane(int crewid, int aircraftid, DateTime tourStartTm)
{
	int errNbr;
	BINTREENODE *tmp;
	LookupRet lkRet;
	RawAircraftData *radPtr, radBuf;
	DateTime pwStartTime;
	//DateTime flight_time;

	DateTime flight_outtime, flight_intime;

	CrewID *cidPtr, cidBuf;
	cidBuf.crewid = crewid;
	tmp = TreeSearch(crewidRoot, &cidBuf, crewidCmp);
	if(tmp)
		cidPtr = getTreeObject(tmp);
	else
		return(0);

	pwStartTime = dt_time_tToDateTime(optParam.windowStart);

	memset(&radBuf, '\0', sizeof(RawAircraftData));
	radBuf.aircraftid = aircraftid;
	radBuf.rec_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", "1970/01/01 00:00", NULL, &errNbr);
    /*DEBUG
	radPtr = (RawAircraftData *) getTreeObject(Minimum(rawAircraftRoot));
	radPtr = (RawAircraftData *) getTreeObject(Maximum(rawAircraftRoot)); */
	lkRet = Lookup(rawAircraftRoot, &radBuf, rawAircraftCompare, &tmp);
	switch(lkRet) {
	case EmptyTree:
	case GreaterThanLastItem:
	case ExactMatchFound: // impossible, since we don't enter other fields in key
			logMsg(logFile,"%s Line %d: aircraft %d not found.\n", __FILE__,__LINE__, radBuf.aircraftid);
			return(0);
	case NotFoundReturnedNextItem:
		for(; tmp; tmp = Successor(tmp)) {
			radPtr = (RawAircraftData *) getTreeObject(tmp);
			if(radPtr->aircraftid != aircraftid)
				break;
			//flight_time = (radPtr->actualout) ? radPtr->actualout : radPtr->rec_outtime;
			flight_outtime = (radPtr->actualout) ? radPtr->actualout : radPtr->rec_outtime;
			flight_intime = (radPtr->actualin) ? radPtr->actualin : radPtr->rec_intime;  //Make sure the leg is actually flown by the pair.
			if((strcmp(radPtr->rowtype,"logmgdleg") == 0 || strcmp(radPtr->rowtype,"mgdleg") == 0) && (flight_outtime < pwStartTime )
					&& flight_outtime > tourStartTm) {
				//if(validateCrewAircraftAssociation(cidPtr->employeenumber, flight_time, aircraftid))
				  if (validateCrewAircraftAssociationFlightLeg(cidPtr->employeenumber, flight_outtime, flight_intime, aircraftid, radPtr->rowtype))
				  //Make sure the leg is actually flown by the pair. RLZ 01/22/2008
				    return(1);
			}

		}
		break;
	}
	return(0);
}

//Roger: this is a temp fix for the hasflownfirst bug. It takes the each of the crew stattime and
//make sure the crew is in the bw_crewassignment.
static int pairHasAlreadyFlownThisPlane(const CrewPairX *cpx, int aircraftID){
	int i;
	bool pairFlownFlag = 0;
	int originalFlag;
	originalFlag = min(hasAlreadyFlownThisPlane(cpx->crewMemA_crewid, aircraftID, cpx->crewMemA_starttime),hasAlreadyFlownThisPlane(cpx->crewMemB_crewid,aircraftID,cpx->crewMemB_starttime));
	
	
	if (!hasAlreadyFlownThisPlane(cpx->crewMemA_crewid, aircraftID, cpx->crewMemA_starttime))
		return 0;

	if (!hasAlreadyFlownThisPlane(cpx->crewMemB_crewid, aircraftID,cpx->crewMemB_starttime))
		return 0;	
	
		for ( i = 0; i < bwPairRecordCount; i++){ //&& bwCrewPairList[i].aircraftid == aircraftID
			if (bwCrewPairList[i].crewid == cpx->crewMemA_crewid && bwCrewPairList[i].aircraftid == aircraftID ) {
				pairFlownFlag = 1; 				
				break;
			}
		}
		if (pairFlownFlag){
			for ( i = 0; i < bwPairRecordCount; i++){//&& bwCrewPairList[i].aircraftid == aircraftID
				if (bwCrewPairList[i].crewid == cpx->crewMemB_crewid && bwCrewPairList[i].aircraftid == aircraftID ) {
					pairFlownFlag = 2; 
					break;
				}
			}
		}
		else return 0;

		if (pairFlownFlag == 2)
			return 1;
		else return 0;

}




typedef struct lpostid_post {
	int lpostid;
	Post post;
} Lpostid_Post;

Lpostid_Post postidToPost[] = {
	{ 10, Post_Unrestricted_PIC  },
	{ 11, Post_Restricted_PIC  },
	{ 12, Post_High_Minimums_PIC  }, // IOE Pairing Status
	{ 13, Post_Standards_Captain  },
	{ 14, Post_Assistant_Chief_Pilot_PIC  },
	{ 15, Post_Office_Management_PIC  },
	{ 16, Post_Unrestricted_PIC  },
	{ 17, Post_Restricted_PIC  },
	{ 18, Post_High_Minimums_PIC  }, // IOE Pairing Status
	{ 19, Post_Standards_Captain  },
	{ 20, Post_Assistant_Chief_Pilot_PIC  },
	{ 21, Post_Office_Management_PIC  },
	{ 22, Post_Unrestricted_PIC  },
	{ 23, Post_Restricted_PIC  },
	{ 24, Post_High_Minimums_PIC  },
	{ 25, Post_Standards_Captain  },
	{ 26, Post_Program_Manager  },
	{ 27, Post_Office_Management_PIC  },
	{ 28, Post_Unrestricted_PIC  },
	{ 29, Post_Restricted_PIC  },
	{ 30, Post_High_Minimums_PIC  }, // IOE Pairing Status
	{ 31, Post_Standards_Captain  },
	{ 32, Post_Assistant_Chief_Pilot_PIC  },
	{ 33, Post_Office_Management_PIC  },
	{ 34, Post_Training  },
	{ 35, Post_Training  },
	{ 36, Post_Training  },
	{ 37, Post_Training  },
	{ 54, Post_Flight_Duty_Officer  },
	{ 55, Post_Flight_Duty_Officer  },
	{ 56, Post_Flight_Duty_Officer  },
	{ 57, Post_Training_Travel  },
	{ 58, Post_Training_Travel  },
	{ 59, Post_Training_Travel  },
	{ 60, Post_Training_Travel  },
	{ 77, Post_Check_Airman  },
	{ 97, Post_Check_Airman  },
	{ 98, Post_Check_Airman  },
	{ 117, Post_Unrestricted_PIC  },
	{ 118, Post_Restricted_PIC  },
	{ 119, Post_High_Minimums_PIC  }, // IOE Pairing Status
	{ 120, Post_Standards_Captain  },
	{ 121, Post_Assistant_Chief_Pilot_PIC  },
	{ 122, Post_Office_Management_PIC  },
	{ 123, Post_Training  },
	{ 124, Post_Training_Travel  },
	{ 137, Post_Check_Airman  },
	{ 157, Post_Unrestricted_PIC  },
	{ 158, Post_Restricted_PIC  },
	{ 159, Post_High_Minimums_PIC  }, // IOE Pairing Status
	{ 160, Post_Standards_Captain  },
	{ 161, Post_Check_Airman  },
	{ 162, Post_Assistant_Chief_Pilot_PIC  },
	{ 163, Post_Office_Management_PIC  },
	{ 164, Post_Training  },
	{ 165, Post_Training_Travel  }
};

static Post
getPostFromPostID(int lpostid)
{
        int n, cond, low, high, mid;

        n = sizeof(postidToPost) / sizeof(Lpostid_Post);

        low = 0;
        high = n - 1;

        while(low <= high) {
                mid = low + (high - low) / 2;
                if((cond = lpostid - postidToPost[mid].lpostid) < 0)
                        high = mid -1;
                else if(cond > 0)
                        low = mid + 1;
                else {
                        return(postidToPost[mid].post);
                }
        }
        return(Post_Null);
}


/*
 0: Check Airman               Post_Check_Airman
 1: Standards Captain          Post_Standards_Captain

 2: Asst Chief Pilot Captain   Post_Assistant_Chief_Pilot - PIC
 3: Office Management Captain  Post_Office_Management - PIC
 4: Unrestrictetd Captain      Post_Unrestricted - PIC
 5: Hi Mins Captain            Post_High_Minimums - PIC
 6: Restricted Captain         Post_Restricted - PIC

 7: Asst Chief Pilot FO        Post_Assistant_Chief_Pilot - SIC
 8: Office Management FO       Post_Office_Management - SIC
 9: Unrestrictetd FO           Post_Unrestricted - SIC
10: Hi Mins FO                 Post_High_Minimums - SIC
11: Restricted FO              Post_Restricted - SIC

*/
static int
getCategoryID(Post post, int position)
{
	switch(post) {
	case Post_Check_Airman:
		return(Post_Check_Airman);

	case Post_Standards_Captain:
		return(Post_Standards_Captain);

	case Post_Assistant_Chief_Pilot_PIC:
		if(position == 1)
			return(Post_Assistant_Chief_Pilot_PIC);
		else
			return(Post_Assistant_Chief_Pilot_SIC);

	case Post_Office_Management_PIC:
		if(position == 1)
			return(Post_Office_Management_PIC);
		else
			return(Post_Office_Management_SIC);

	case Post_Unrestricted_PIC:
		if(position == 1)
			return(Post_Unrestricted_PIC);
		else
			return(Post_Unrestricted_SIC);

	case Post_High_Minimums_PIC:
		if(position == 1)
			return(Post_High_Minimums_PIC);
		else
			return(Post_High_Minimums_SIC);

	case Post_Restricted_PIC:
		if(position == 1)
			return(Post_Restricted_PIC);
		else
			return(Post_Restricted_SIC);
	default:
		break;
	}
	return(-1);
}

static void
getCrewCategoryID(PRE_Crew *preCrewPtr)
{
	SS_CrewData *ssCdPtr;
	BINTREENODE *tmpSS;
	Post post;

	for(tmpSS = Minimum(preCrewPtr->ssCrewDataRoot); tmpSS; tmpSS = Successor(tmpSS)) {
		ssCdPtr = (SS_CrewData *) getTreeObject(tmpSS);
		//if(SSdayInRange(preCrewPtr->tourStartTm, ssCdPtr->dtdate, preCrewPtr->tourEndTm))
		//if(EndOfDayInGMT(ssCdPtr->dtdate) > preCrewPtr->tourStartTm && BeginningOfDayInGMT(ssCdPtr->dtdate) < preCrewPtr->tourEndTm) {
		if(ssCdPtr->dtdate >= preCrewPtr->tourStartTm && ssCdPtr->dtdate <= preCrewPtr->tourEndTm) {
			if((post = getPostFromPostID(ssCdPtr->lpostid)) >= 0) {
				if((preCrewPtr->categoryID = getCategoryID(post, preCrewPtr->position)) >= 0) {
					return;
				}
			}
		}
	}

	// we end up here if we haven't yet set the categoryID.
	// try again not worrying about whether the day in question is within tourStart and tourEnd
	for(tmpSS = Minimum(preCrewPtr->ssCrewDataRoot); tmpSS; tmpSS = Successor(tmpSS)) {
		ssCdPtr = (SS_CrewData *) getTreeObject(tmpSS);
		if((post = getPostFromPostID(ssCdPtr->lpostid)) >= 0) {
			if((preCrewPtr->categoryID = getCategoryID(post, preCrewPtr->position)) >= 0) {
				return;
			}
		}
	}

	preCrewPtr->categoryID = -1;
}

static Post
catBitToPost(unsigned catbit)
{

	switch(catbit) {
	case Bit_Check_Airman:
        	return(Post_Check_Airman);
	case Bit_Standards_Captain:
        	return(Post_Standards_Captain);
	case Bit_Assistant_Chief_Pilot:
        	return(Post_Assistant_Chief_Pilot_PIC);
	case Bit_Office_Management:
        	return(Post_Office_Management_PIC);
	case Bit_Unrestricted:
        	return(Post_Unrestricted_PIC);
	case Bit_High_Minimums:
        	return(Post_High_Minimums_PIC);
	case Bit_Restricted:
        	return(Post_Restricted_PIC);
	default:
		break;
	}
	return(Post_Null);
}

static void
showCrewHeader(void)
{
fprintf(logFile,"crewList:\n");
fprintf(logFile,"+---------+-----+------+------------------+------------------+-------+-------+-------+-------+-------+------------------+------+-------+-------+-----+\n");
fprintf(logFile,"|         |     | ac   | tour             | tour             | start |  stay | start | end   | avail |                  | act  | duty  | block | cat |\n");
fprintf(logFile,"|  crewID | pos | type | starttime        | endtime          | Early |  late | loc   | loc   | apt   | availDT          | code | time  | time  |  id |\n");
fprintf(logFile,"+---------+-----+------+------------------+------------------+-------+-------+-------+-------+-------+------------------+------+-------+-------+-----+\n");
}

static void
showCrewFooter(void)
{
fprintf(logFile,"+---------+-----+------+------------------+------------------+-------+-------+-------+-------+------------------+------+-------+-------+-----+\n");
}

static void
showCrew(Crew *cPtr)
{
	int errNbr;
	char tbuf1[32];
	char tbuf2[32];
	char tbuf3[32];
	char tbuf4[32];
	char tbuf5[32];
	AirportLatLon *aPtr, *aPtr1, *aPtr0;
	int hours, minutes;
	Time blocktime, dutytime;

	aPtr0 = getAirportLatLonInfoByAptID(cPtr->startLoc);
	aPtr = getAirportLatLonInfoByAptID(cPtr->endLoc);
	aPtr1 = getAirportLatLonInfoByAptID(cPtr->availAirportID);

	hours = cPtr->blockTm / 60;
	minutes = cPtr->blockTm % 60;
	blocktime = dt_HMSMtoTime(hours, minutes, 0, 0);

	hours = cPtr->dutyTime / 60;
	minutes = cPtr->dutyTime % 60;
	dutytime = dt_HMSMtoTime(hours, minutes, 0, 0);

	fprintf(logFile,"| %7d | %3d | %4d | %s | %s | %5.2f | %5.2f | %5s | %5s | %5s | %s | %4d | %s | %s | %3d |\n",
		cPtr->crewID,
		cPtr->position,
		cPtr->aircraftTypeID,
		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
			asctime(gmtime(&(cPtr->tourStartTm))), NULL, &errNbr),tbuf1,"%Y/%m/%d %H:%M"),
		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
			asctime(gmtime(&(cPtr->tourEndTm))), NULL, &errNbr),tbuf2,"%Y/%m/%d %H:%M"),
		cPtr->startEarly,
		cPtr->stayLate,
		(aPtr0) ? aPtr0->icao : "",
		(aPtr) ? aPtr->icao : "",
		(aPtr1) ? aPtr1->icao : "",
		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
			asctime(gmtime(&(cPtr->availDT))), NULL, &errNbr),tbuf3,"%Y/%m/%d %H:%M"),
		cPtr->activityCode,
		dt_TimeToTimeString(dutytime, tbuf4,"%H:%M"),
		dt_TimeToTimeString(blocktime, tbuf5,"%H:%M"),
		cPtr->categoryID);
}

/*
 *
 * onTime = 0 - get scheduled off
 * onTime = 1 - get scheduled on
 * onTime = 2 - get pointer to record
 *
 */

//NOT USED ANY MORE
static DateTime
getScheduledOnOff(PRE_Crew *preCrewPtr, DateTime dateToCheck, int onTime, CS_CrewData **cs_cdPPtr)
{
	CS_CrewData *cs_cdPtr;
	BINTREENODE *tmp_cscd;
	DateTime tdate;

	if(cs_cdPPtr)
		*cs_cdPPtr = NULL;
	for(tmp_cscd = Minimum(preCrewPtr->csCrewDataRoot); tmp_cscd; tmp_cscd = Successor(tmp_cscd)) {
		cs_cdPtr = (CS_CrewData *) getTreeObject(tmp_cscd);
		tdate = ((cs_cdPtr->actual_on) ? cs_cdPtr->actual_on : cs_cdPtr->scheduled_on);
		if((dateToCheck & DateOnly) == (tdate & DateOnly)) {
			if(onTime == 2) {
				if(cs_cdPPtr)
					*cs_cdPPtr = cs_cdPtr;
				return((DateTime) NULL);
			}
			else if(onTime == 1) {
				// get scheduled on
				return(tdate);
			}
			else {
				// get scheduled off
				tdate = ((cs_cdPtr->actual_off) ? cs_cdPtr->actual_off : cs_cdPtr->scheduled_off);
				return(tdate);
			}
		}
	}
	return(BadDateTime);
}
static void
getTourStartEnd(PRE_Crew *preCrewPtr)
{
	DateTime pwStartDate, pwEndDate, tmpDate;
	BINTREENODE *tmp, *svTmp;
	SS_CrewData *ssCdPtr;
	char lbuf[128];

	pwStartDate = dt_time_tToDateTime(optParam.windowStart);
	pwEndDate = dt_time_tToDateTime(optParam.windowEnd);
	

	// find date within the planning window that does not have a special zacccodeid and can fly
	for(tmp = Minimum(preCrewPtr->ssCrewDataRoot); tmp; tmp = Successor(tmp)) {
		ssCdPtr = (SS_CrewData *) getTreeObject(tmp);

		// keep going if they are doing training or office work
		strcpy(lbuf, returnUpper(ssCdPtr->zpostdesc));
		if(strstr(lbuf,"REC NOT IN"))
			strcpy(lbuf, "flying");
        //gmtDayInSSdayRange, SSdayInRange
		if(gmtDayInSSdayRange1(pwStartDate, ssCdPtr->dtdate, pwEndDate) && strlen(ssCdPtr->zacccodeid) == 0 && !cantFly(lbuf))
		//if(pwStartDate <= AddDays(1, ssCdPtr->dtdate) && pwEndDate >= ssCdPtr->dtdate && strlen(ssCdPtr->zacccodeid) == 0 && !cantFly(lbuf))
			break;
	}
	if(tmp) {
		// we found an ss day without a zacccodeid value (OT) within the planning window and crew member can fly
		// back up until prior day is not current day -1 or until we hit an OT (zacccodeid) or non fly day (like training)
		svTmp = tmp;
		tmpDate = ssCdPtr->dtdate;
		tmpDate = AddDays(-1, tmpDate);
		for(tmp = Predecessor(tmp); tmp; tmp = Predecessor(tmp)) {
			ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
			if(tmpDate != ssCdPtr->dtdate)
				break;
			if(strlen(ssCdPtr->zacccodeid))
				break;
			strcpy(lbuf, returnUpper(ssCdPtr->zpostdesc)); // stop if they are doing training or office work
			if(!strstr(lbuf,"REC NOT IN")) {
				if(cantFly(lbuf))
					break;
			}
			preCrewPtr->tourStartTm = ssCdPtr->dtdate; 
			tmpDate = AddDays(-1, tmpDate);
		}
		if(!tmp) {
			// svTmp is tourStart if we didn't set preCrewPtr->tourStartTm above.
			if(! preCrewPtr->tourStartTm) {
				ssCdPtr = (SS_CrewData *) getTreeObject(svTmp);
				preCrewPtr->tourStartTm = ssCdPtr->dtdate; //dt_addToDateTime(Hours,3,BeginningOfDayInGMT(ssCdPtr->dtdate));
			}
		}
		else {
			// Succeeding day is tour start
			tmp = Successor(tmp);
			ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
			preCrewPtr->tourStartTm = ssCdPtr->dtdate; //dt_addToDateTime(Hours,3,BeginningOfDayInGMT(ssCdPtr->dtdate));
		}
	}
	else {
		// is there an ss day with a zacccodeid value (OT) within the planning window where crew member can fly?
		for(tmp = Minimum(preCrewPtr->ssCrewDataRoot); tmp; tmp = Successor(tmp)) {
			ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
			
			//if(pwStartDate <= AddDays(1, ssCdPtr->dtdate) && pwEndDate >= ssCdPtr->dtdate) {
			if(gmtDayInSSdayRange1(pwStartDate, ssCdPtr->dtdate, pwEndDate)) {
				strcpy(lbuf, returnUpper(ssCdPtr->zpostdesc));
				if(strstr(lbuf,"REC NOT IN"))
					strcpy(lbuf, "flying");
				if(strlen(ssCdPtr->zacccodeid) && ! cantFly(lbuf))
					break;
			}
		}
		if(tmp) {
			// the only ss days in the planning window where crew member can fly have a zacccodeid value (OT)
			// back up until prior day is not current day -1 and prior day is not training

			// need to tell if the OT is in tourEnd or TourStart
			svTmp = tmp;
			tmpDate = ssCdPtr->dtdate;
			tmpDate = AddDays(-1, tmpDate);
			tmp = Predecessor(tmp);
			//if (tmp) ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
			if (tmp){ // OT is at the end of tour
			//	ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
			//	if (tmpDate == ssCdPtr->dtdate){
				for(; tmp; tmp = Predecessor(tmp)) {
					ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
					if(tmpDate != ssCdPtr->dtdate)
						break;
					if(strlen(ssCdPtr->zacccodeid))  //RLZ 02/29/08
						break;
					strcpy(lbuf, returnUpper(ssCdPtr->zpostdesc)); // stop if they are doing training or office work
					if(!strstr(lbuf,"REC NOT IN")) {
						if(cantFly(lbuf))
							break;
					}

					preCrewPtr->tourStartTm = ssCdPtr->dtdate; //dt_addToDateTime(Hours,3,BeginningOfDayInGMT(ssCdPtr->dtdate));
					tmpDate = AddDays(-1, tmpDate);
				}
				if(!tmp) {
					// svTmp is tourStart if we didn't set preCrewPtr->tourStartTm above.		
					// THIS SHOULD NEVER HAPPEN, RLZ 10/11/2007
					if(! preCrewPtr->tourStartTm) { 
						ssCdPtr = (SS_CrewData *) getTreeObject(svTmp);
						preCrewPtr->tourStartTm = ssCdPtr->dtdate; //dt_addToDateTime(Hours,3,BeginningOfDayInGMT(ssCdPtr->dtdate));
					}
				}
				else { //the current day is train, travel.
					// Succeeding day is tour start
					if(tmpDate == ssCdPtr->dtdate){
						tmp = Successor(tmp);
						ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
						preCrewPtr->tourStartTm = ssCdPtr->dtdate; //dt_addToDateTime(Hours,3,BeginningOfDayInGMT(ssCdPtr->dtdate));
					}
					else {//OT is at the begining of the tour
						for(tmp = Successor(svTmp); tmp; tmp = Successor(tmp)) {
							ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
							strcpy(lbuf, returnUpper(ssCdPtr->zpostdesc));
							if(strstr(lbuf,"REC NOT IN"))
								strcpy(lbuf, "flying");
							if(!strlen(ssCdPtr->zacccodeid) && ! cantFly(lbuf)){
								if(! preCrewPtr->tourStartTm) {
									//ssCdPtr = (SS_CrewData *) getTreeObject(svTmp);
									preCrewPtr->tourStartTm = ssCdPtr->dtdate; //dt_addToDateTime(Hours,3,BeginningOfDayInGMT(ssCdPtr->dtdate));
								}
								break;
							}
						}			
					}

				}
			}
			else { // OT is at the beginning of tour

				for(tmp = Successor(svTmp); tmp; tmp = Successor(tmp)) { //Must have Successor, and find the regular tour day 
					ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
					strcpy(lbuf, returnUpper(ssCdPtr->zpostdesc));
					if(strstr(lbuf,"REC NOT IN"))
						strcpy(lbuf, "flying");
					if(!strlen(ssCdPtr->zacccodeid) && ! cantFly(lbuf)){
						if(! preCrewPtr->tourStartTm) {
							//ssCdPtr = (SS_CrewData *) getTreeObject(svTmp);
							preCrewPtr->tourStartTm = ssCdPtr->dtdate; //dt_addToDateTime(Hours,3,BeginningOfDayInGMT(ssCdPtr->dtdate));
						}
						break;
					}
				}
				// Succeeding day is tour start
				//tmp = Successor(svTmp);
				//ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
				//preCrewPtr->tourStartTm = ssCdPtr->dtdate; //dt_addToDateTime(Hours,3,BeginningOfDayInGMT(ssCdPtr->dtdate));
			 }
		}

		else {
			// there are no ss days within the planning window
			preCrewPtr->availDuringPlanningWindow = 0;
		}
	}
	if(preCrewPtr->availDuringPlanningWindow == 0)
		return;


	//find tour end. start at tour start
	for(tmp = Minimum(preCrewPtr->ssCrewDataRoot); tmp; tmp = Successor(tmp)) {
		ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
		//if((ssCdPtr->dtdate & DateOnly) == (preCrewPtr->tourStartTm & DateOnly))
		if(ssCdPtr->dtdate == preCrewPtr->tourStartTm)
			break;
	}
	if(! tmp) {
		preCrewPtr->availDuringPlanningWindow = 0;
		logMsg(logFile,"%s Line %d: lost preCrewPtr->tourStartTm: %s.\n", __FILE__,__LINE__, preCrewPtr->employeenumber);
		return;
	}
	svTmp = tmp;
	tmpDate = ssCdPtr->dtdate;
	tmpDate = AddDays(1, tmpDate);
	for(tmp = Successor(tmp); tmp; tmp = Successor(tmp)) {
		ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
		if(tmpDate != ssCdPtr->dtdate)
			break;

		// stop if they are doing training or office work
		strcpy(lbuf, returnUpper(ssCdPtr->zpostdesc));
		if(strstr(lbuf,"REC NOT IN"))
			strcpy(lbuf, "flying");

		if(strlen(ssCdPtr->zacccodeid) > 0 || cantFly(lbuf))
			break;
		//preCrewPtr->tourEndTm = dt_addToDateTime(Minutes,optParam.maxDutyTm, dt_addToDateTime(Hours,3,BeginningOfDayInGMT(ssCdPtr->dtdate)));
//Roger fix for get home to be local 3 AM.
		preCrewPtr->tourEndTm = dt_addToDateTime(Hours,24,ssCdPtr->dtdate); //dt_addToDateTime(Hours,24,BeginningOfDayInGMT(ssCdPtr->dtdate));
		//preCrewPtr->tourEndTm += timeZoneAdjByApt(preCrewPtr->endLoc, preCrewPtr->tourEndTm);
		tmpDate = AddDays(1, tmpDate);
	}
	if(!tmp) {
		// svTmp is tour end if we didn't set preCrewPtr->tourEndTm above.
		if(! preCrewPtr->tourEndTm) {
			ssCdPtr = (SS_CrewData *) getTreeObject(svTmp);
			//preCrewPtr->tourEndTm = dt_addToDateTime(Minutes,optParam.maxDutyTm, dt_addToDateTime(Hours,3,BeginningOfDayInGMT(ssCdPtr->dtdate)));
//Roger
		preCrewPtr->tourEndTm = dt_addToDateTime(Hours,24,ssCdPtr->dtdate); //dt_addToDateTime(Hours,24,BeginningOfDayInGMT(ssCdPtr->dtdate));
		//preCrewPtr->tourEndTm += timeZoneAdjByApt(preCrewPtr->endLoc, preCrewPtr->tourEndTm);

		}
	}
	else {
		// Preceeding day is tour end
		tmp = Predecessor(tmp);
		ssCdPtr = (SS_CrewData *) getTreeObject(tmp);
		//preCrewPtr->tourEndTm = dt_addToDateTime(Minutes,optParam.maxDutyTm, dt_addToDateTime(Hours,3,BeginningOfDayInGMT(ssCdPtr->dtdate)));
//Roger
		preCrewPtr->tourEndTm = dt_addToDateTime(Hours,24,ssCdPtr->dtdate); //dt_addToDateTime(Hours,24,BeginningOfDayInGMT(ssCdPtr->dtdate));
		//preCrewPtr->tourEndTm += timeZoneAdjByApt(preCrewPtr->endLoc, preCrewPtr->tourEndTm);

	}
}

static int
timeZoneAdjByApt(int AirportID, DateTime gmt)
{
	AirportLatLon *allPtr;
	DateTime lt1, lt2;
	int lt1_t, lt2_t;
	int isLDT;

	allPtr = getAirportLatLonInfoByAptID(AirportID);

	lt1 = gmtToLocal(55, gmt, &isLDT);
	lt1_t = (int) DateTimeToTime_t(lt1);

	lt2 = gmtToLocal((allPtr) ? allPtr->tzid : 55, gmt, &isLDT);
	lt2_t = (int) DateTimeToTime_t(lt2);

	return(lt1_t - lt2_t);
}


// incremental revenue functions
static void
readUpgradeDowngrade(MY_CONNECTION *myconn)
{
	AirportLatLon *outAptPtr, *inAptPtr;
	FlightCalcOutput *contractFcoPtr, *requestedFcoPtr, *fcoPtr;
	FlightCalcOutput fcoBuf[MAX_AC_TYPES + 1];
	DateTimeParts dtp;
    char writetodbstring1[200];
    char sqlBuf[1024]; // 03/30/09 ANG
	int acTypeToCompare;
	// should select fields, instead of *

//	char *upgradeDowngradeSQL = "select ud.*, dmd.outaptid, dmd.inaptid, dmd.numberofpassengers from upgradedowngrade ud, demand dmd where dmd.demandid = ud.demandid and legkeywordid != 0";
	//maxDemandID: only care about the demand in the planning window range. RLZ 10/22/2007
//	char *ratiosSQL = "select * from ratios";

	char tbuf[32], tbuf1[32], tbuf2[32], tbuf3[32];
	//MYSQL_RES *res;
	//MYSQL_FIELD *cols;
	//MYSQL_ROW row;
	//my_ulonglong rowCount, rows;
	int errNbr = 0;

	int i, j, count;
	QITEM *qi;
	DemandInfo *diPtr; //diBuf;
	Demand *dmdPtr, dmdBuf;
	KeyWord kw;
	//keyword_rec_type kwrt;
	KW_Rec *kwRecPtr; //kwRecBuf;
	//int demandid;
	BINTREENODE *tmp, *tmp1, *tmp2;
	Ratios *rPtr, rBuf;

	if(oracleDirect == 1)
        getUpgradeDowngradeDataFromOracle();
	else if(oracleDirect == 2)
		getUpgradeDowngradeDataFromOracleDB(orl_socket);
	else
		getUpgradeDowngradeData(myconn);





//	if(!myDoQuery(myconn, ratiosSQL, &res, &cols)) {
//		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
//		writeWarningData(myconn); exit(1);
//	}
//
//	if(! res) {
//		logMsg(logFile,"%s Line %d: readUpgradeDowngrade(): no results.\n", __FILE__,__LINE__);
//		writeWarningData(myconn); exit(1);
//	}
//
//	rowCount = mysql_num_rows(res);
//	if(! rowCount) {
//		logMsg(logFile,"%s Line %d: readUpgradeDowngrade(): 0 rows returned.\n", __FILE__,__LINE__);
//		writeWarningData(myconn); exit(1);
//	}
//#ifdef DEBUGGING
//	logMsg(logFile,"raw query results for ratiosSQL:\n");
//	displayResults(res, cols);
//	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
//#endif // DEBUGGING
//
//	for(rows = 0; rows < rowCount; rows++) {
//		row = mysql_fetch_row(res);
//		if(! row)
//			break;
//		memset(&rBuf, '\0', sizeof(rBuf));
//		rBuf.contractid = atoi(row[rt_contractid]);
//		tmp = TreeSearch(ratiosRoot, &rBuf, contractidcmp);
//		if(tmp) {
//			// contractid already exists in tree
//			rPtr = (Ratios *) getTreeObject(tmp);
//			rPtr->ratios[ac_conv(AC_TYPE_ID, atoi(row[rt_actypeid]), IDX_POS)] = atof(row[rt_ratio]);
//			continue;
//		}
//
//		rPtr = calloc((size_t) 1, sizeof(Ratios));
//
//		if(! rPtr) {
//			logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
//			writeWarningData(myconn); exit(1);
//		}
//		rPtr->contractid = atoi(row[rt_contractid]);
//		rPtr->contract_actypeid = atoi(row[rt_contract_actypeid]);
//		rPtr->actypeid = atoi(row[rt_actypeid]);
//		rPtr->ratios[ac_conv(AC_TYPE_ID, rPtr->actypeid, IDX_POS)] = atof(row[rt_ratio]);
//		if(!(ratiosRoot = RBTreeInsert(ratiosRoot, rPtr, contractidcmp))) {
//			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//			writeWarningData(myconn); exit(1);
//		}
//	}
//
//	// free mysql results
//	mysql_free_result(res);
//
//	if(!myDoQuery(myconn, upgradeDowngradeSQL, &res, &cols)) {
//		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
//		writeWarningData(myconn); exit(1);
//	}
//
//	if(! res) {
//		logMsg(logFile,"readUpgradeDowngrade(): no results.\n");
//		writeWarningData(myconn); exit(1);
//	}
//
//	rowCount = mysql_num_rows(res);
//	if(! rowCount) {
//		logMsg(logFile,"readUpgradeDowngrade(): 0 rows returned.\n");
//		writeWarningData(myconn); exit(1);
//	}
//#ifdef DEBUGGING
//	logMsg(logFile,"raw query results for upgradeDowngradeSQL:\n");
//	displayResults(res, cols);
//	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
//#endif // DEBUGGING
//
//	for(rows = 0; rows < rowCount; rows++) {
//		row = mysql_fetch_row(res);
//		if(! row)
//			break;
//
//		if(strcmp(row[ud_type], "demand") == 0)
//			kwrt = kw_demand;
//		else if(strcmp(row[ud_type], "mgdleg") == 0)
//			kwrt = kw_mgdleg;
//		else if(strcmp(row[ud_type], "lpgleg") == 0)
//			kwrt = kw_lpgleg;
//		else {
//			logMsg(logFile,"%s Line %d, unexpected value for \"type\" in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//			writeWarningData(myconn); exit(1);
//		}
//		if(row[ud_legkeywordid])
//			kw = atoi(row[ud_legkeywordid]);
//
//		/*
//			first test for DemandInfo already existing in tree for demandid
//		*/
//
//		if(row[ud_demandid]) {
//			if(!(demandid = atoi(row[ud_demandid]))) {
//				logMsg(logFile,"%s Line %d, zero demandid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//		else {
//			logMsg(logFile,"%s Line %d, null demandid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//			writeWarningData(myconn); exit(1);
//		}
//
//		memset(&diBuf, '\0', sizeof(diBuf));
//		diBuf.demandid = demandid;
//		tmp = TreeSearch(dmdKeyWdRoot, &diBuf, demandidcmp);
//		if(tmp) {
//			// demandid already exists in tree
//			diPtr = (DemandInfo *) getTreeObject(tmp);
//			kwRecBuf.kwrt = kwrt;
//			kwRecBuf.kw = kw;
//			if(! kwExists(kw, diPtr->kwq)) {
//				// new keyword
//				if(! (kwRecPtr = (KW_Rec *) calloc((size_t) 1, sizeof(KW_Rec)))) {
//					logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
//					writeWarningData(myconn); exit(1);
//				}
//				kwRecPtr->kwrt = kwrt;
//				kwRecPtr->kw = kw;
//				(void *) QAddToTail(diPtr->kwq, kwRecPtr);
//			}
//			continue;
//		}
//		else {
//			// new demandid
//			diPtr = calloc((size_t) 1, sizeof(DemandInfo));
//
//			if(! diPtr) {
//				logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//			diPtr->kwq = createQueue();
//			if(! (kwRecPtr = calloc((size_t) 1, sizeof(KW_Rec)))) {
//				logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//			kwRecPtr->kwrt = kwrt;
//			kwRecPtr->kw = kw;
//			(void *) QAddToTail(diPtr->kwq, kwRecPtr);
//		}
//
//		if(row[ud_ownerid]) {
//			if(!(diPtr->ownerid = atoi(row[ud_ownerid]))) {
//				logMsg(logFile,"%s Line %d, zero ownerid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//		if(row[ud_contractid]) {
//			if(!(diPtr->contractid = atoi(row[ud_contractid]))) {
//				logMsg(logFile,"%s Line %d, zero contractid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//		if(row[ud_dmd_outtime]) {
//			if((diPtr->dmd_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[ud_dmd_outtime], NULL, &errNbr)) == BadDateTime) {
//				logMsg(logFile,"%s Line %d, Bad date in readUpgradeDowngrade(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[ud_dmd_outtime]);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//		if(row[ud_outtime]) {
//			if((diPtr->outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[ud_outtime], NULL, &errNbr)) == BadDateTime) {
//				logMsg(logFile,"%s Line %d, Bad date in readUpgradeDowngrade(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[ud_outtime]);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//
//		diPtr->demandid = demandid;
//
//		if(row[ud_otherid]) {
//			if(!(diPtr->otherid = atoi(row[ud_otherid]))) {
//				logMsg(logFile,"%s Line %d, zero otherid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//		if(!(diPtr->shortname = strdup(row[ud_shortname]))) {
//			logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//			writeWarningData(myconn); exit(1);
//		}
//		if(row[ud_contract_seqpos]) {
//			if(!(diPtr->contract_seqpos = atoi(row[ud_contract_seqpos]))) {
//				logMsg(logFile,"%s Line %d, zero contract_seqpos in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//		if(row[ud_contract_actypeid]) {
//			if(!(diPtr->contract_actypeid = atoi(row[ud_contract_actypeid]))) {
//				logMsg(logFile,"%s Line %d, zero contract_actypeid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//		if(row[ud_seqpos]) {
//			if(!(diPtr->seqpos = atoi(row[ud_seqpos]))) {
//				logMsg(logFile,"%s Line %d, zero seqpos in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//		if(row[ud_actypeid]) {
//			if(!(diPtr->actypeid = atoi(row[ud_actypeid]))) {
//				logMsg(logFile,"%s Line %d, zero actypeid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//		if(row[ud_outaptid]) {
//			if(!(diPtr->outaptid = atoi(row[ud_outaptid]))) {
//				logMsg(logFile,"%s Line %d, zero outaptid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//		if(row[ud_inaptid]) {
//			if(!(diPtr->inaptid = atoi(row[ud_inaptid]))) {
//				logMsg(logFile,"%s Line %d, zero inaptid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//				writeWarningData(myconn); exit(1);
//			}
//		}
//		if(row[ud_nbrPsgrs])
//			diPtr->nbrPsgrs = atoi(row[ud_nbrPsgrs]);
//		if(!(dmdKeyWdRoot = RBTreeInsert(dmdKeyWdRoot, diPtr, demandidcmp))) {
//			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readUpgradeDowngrade().\n",__FILE__,__LINE__);
//			writeWarningData(myconn); exit(1);
//		}
//	}
//
//	// free mysql results
//	mysql_free_result(res);

	/* get smallest aircraft that can be used to fulfill the demand */
	for(tmp = Minimum(dmdKeyWdRoot); tmp; tmp = Successor(tmp)) {
		diPtr = (DemandInfo *) getTreeObject(tmp);

		//
		// get smallest aircraft that can be used to fulfill the demand
		//

		if(upgradeKeywords(diPtr->kwq) && downgradeKeywords(diPtr->kwq))
			;
		else if(! upgradeDowngradeKeywords(diPtr->kwq) && ! kwDowngrade(diPtr) && ! kwUpgrade(diPtr)) {     // 0 0 0
			// no upgrade/downgrade keywords, scheduled actype matches contract
  			diPtr->smallestAllowableSeqPos = diPtr->contract_seqpos;
		}
		else if(! upgradeDowngradeKeywords(diPtr->kwq) && ! kwDowngrade(diPtr) && kwUpgrade(diPtr)) {  // 0 0 1
			// no upgrade/downgrade keywords, scheduled actype upgraded from contract
			// contract plane, ? because upgrade wasn't requested
  			diPtr->smallestAllowableSeqPos = diPtr->contract_seqpos;
  			diPtr->smallestAllowableIssue = 1;
		}
		else if(! upgradeDowngradeKeywords(diPtr->kwq) && kwDowngrade(diPtr) && ! kwUpgrade(diPtr)) {  // 0 1 0
			// no upgrade/downgrade keywords, scheduled actype downgraded from contract
			// downgraded plane is currently scheduled. We will show contract plane as smallest.
			// "?" because currently scheduled downgrade wasn't requested
  			diPtr->smallestAllowableSeqPos = diPtr->contract_seqpos;
  			diPtr->smallestAllowableIssue = 2;
		}

		// 0 1 1 can't happen

		else if(upgradeDowngradeKeywords(diPtr->kwq) && ! kwDowngrade(diPtr) && ! kwUpgrade(diPtr)) {  // 1 0 0
			if((kw = kwDowngradeRequest(diPtr->kwq))) {
				// downgrade requested, scheduled actype matches contract
  				diPtr->smallestAllowableSeqPos = ac_conv(DOWNGRADE, kw, SEQ_POS);
			}
			else if((kw = kwUpgradeRequest(diPtr->kwq))) {
				// upgrade requested, scheduled actype matches contract
				if(kwExists(KW_Upgrade_Approved, diPtr->kwq) || kwExists(KW_Guaranteed_Upgrade, diPtr->kwq)) {
					// upgrade has been guaranteed or approved, smallest allowable is the upgraded plane
					// ? because upgraded plane isn't scheduled
  					diPtr->smallestAllowableSeqPos = ac_conv(UPGRADE, kw, SEQ_POS);
  					diPtr->smallestAllowableIssue = 3;
				}
				else {
					// upgrade has not been guaranteed or approved, smallest allowable is the contract plane
  					diPtr->smallestAllowableSeqPos = diPtr->contract_seqpos;
					diPtr->largestAllowableSeqPos = max(diPtr->largestAllowableSeqPos, ac_conv(UPGRADE, kw, SEQ_POS)); //RLZ
				}
			}
		}
		else if(upgradeDowngradeKeywords(diPtr->kwq) && ! kwDowngrade(diPtr) && kwUpgrade(diPtr)) {  // 1 0 1
			if((kw = kwUpgradeRequest(diPtr->kwq))) {
				// upgrade requested, an upgrade is scheduled
				if(kwExists(KW_Upgrade_Approved, diPtr->kwq) || kwExists(KW_Guaranteed_Upgrade, diPtr->kwq)) {
					// upgrade has been guaranteed or approved, smallest allowable is the upgraded plane
					if(diPtr->seqpos == ac_conv(UPGRADE, kw, SEQ_POS)) {
						// scheduled upgraded plane matches plane requested
  						diPtr->smallestAllowableSeqPos = diPtr->seqpos;
					}
					else {
						// set smallestAllowable to what was requested
						// ? because scheduled upgraded plane does not match plane requested
						diPtr->smallestAllowableSeqPos = ac_conv(UPGRADE, kw, SEQ_POS);
  						diPtr->smallestAllowableIssue = 4;
					}
				}
				else { // upgrade requested, has not been guaranteed or approved
					// smallest is contract plane
					diPtr->smallestAllowableSeqPos = diPtr->contract_seqpos;
					diPtr->largestAllowableSeqPos = max(diPtr->largestAllowableSeqPos, ac_conv(UPGRADE, kw, SEQ_POS)); //RLZ
				}
			}
			else if((kw = kwDowngradeRequest(diPtr->kwq))) {
				// set smallestAllowable to requested downgrade.
				// ? because downgrade requested, upgrade scheduled
				diPtr->smallestAllowableSeqPos = ac_conv(DOWNGRADE, kw, SEQ_POS);
  				diPtr->smallestAllowableIssue = 5;
			}
			else if(kwExists(KW_Upgrade_Approved, diPtr->kwq) || kwExists(KW_Guaranteed_Upgrade, diPtr->kwq)) {
				// no upgrade requested but have guaranteed upgrade or upgrade approval
				// set smallest allowable to the scheduled upgraded plane.
				diPtr->smallestAllowableSeqPos = diPtr->seqpos;
			}
		}
		else if(upgradeDowngradeKeywords(diPtr->kwq) && kwDowngrade(diPtr) && ! kwUpgrade(diPtr)) {  // 1 1 0
			if((kw = kwDowngradeRequest(diPtr->kwq))) {
				// downgrade requested, downgrade is scheduled
				if(diPtr->seqpos == ac_conv(DOWNGRADE, kw, SEQ_POS)) {
					// scheduled downgrade matches plane requested
					diPtr->smallestAllowableSeqPos = diPtr->seqpos;
				}
				else {
					// scheduled downgrade does not match plane requested
					// set smallestAllowable to requested plane
					diPtr->smallestAllowableSeqPos = ac_conv(DOWNGRADE, kw, SEQ_POS);

					if(diPtr->seqpos < diPtr->smallestAllowableSeqPos)
						// ? because scheduled plane less than requested downgrade
						diPtr->smallestAllowableIssue = 6;
				}
			}
			else if((kw = kwUpgradeRequest(diPtr->kwq))) {
				if(kwExists(KW_Upgrade_Approved, diPtr->kwq) || kwExists(KW_Guaranteed_Upgrade, diPtr->kwq)) {
					// set smallestAllowable to requested upgrade, because upgrade is approved or guaranteed
					diPtr->smallestAllowableSeqPos = ac_conv(UPGRADE, kw, SEQ_POS);
				}
				else {
					// set smallestAllowable to contract plane, upgrade request is not approved
					diPtr->smallestAllowableSeqPos = diPtr->contract_seqpos;
					diPtr->largestAllowableSeqPos = max(diPtr->largestAllowableSeqPos, ac_conv(UPGRADE, kw, SEQ_POS)); //RLZ
				}
				// ? because upgrade requested, downgrade scheduled
				diPtr->smallestAllowableIssue = 7;
			}
			else if(kwExists(KW_Downgrade_Approved, diPtr->kwq) || kwExists(KW_Guaranteed_Downgrade, diPtr->kwq)) {
				// no downgrade requested but have guaranteed downgrade or downgrade approval
				// smallest allowable is the downgraded plane
				diPtr->smallestAllowableSeqPos = diPtr->seqpos;
			}
		}

		if (kwExists(KW_Value_Plus, diPtr->kwq)){ //RLZ: Value Plus contract  10/22/2007
			diPtr->smallestAllowableSeqPos = diPtr->seqpos; // This will work well if upgrade/downgrade only from demand. 
			diPtr->smallestAllowableIssue = 99;
		}

		
		// 1 1 1 can't happen
		if(! diPtr->smallestAllowableSeqPos) {
			// set to contract plane
			diPtr->smallestAllowableSeqPos = diPtr->contract_seqpos;
			// ? because we didn't fall through any of the logic above
			diPtr->smallestAllowableIssue = 8;
		}

	}
	if(verbose) {
		logMsg(logFile,"\n\nFlight Calculator Results:\n");
		fprintf(logFile,"+--------+------+------+------+------+------+\n");
		fprintf(logFile,"| Demand |  CJ1 | Brav |  CJ3 |  Exc |  Sov |\n");
		fprintf(logFile,"+--------+------+------+------+------+------+\n");
	}
	for(tmp = Minimum(dmdKeyWdRoot); tmp; tmp = Successor(tmp)) {
		diPtr = (DemandInfo *) getTreeObject(tmp);
		dmdBuf.demandID = diPtr->demandid;
		tmp2 = TreeSearch(dmdXDemandIdRoot,&dmdBuf,demandIdCompare);
		if(! tmp2) {
			// didn't find this demandid.
			// not in planning window
			continue;
		}
		dmdPtr = (Demand *) getTreeObject(tmp2);
  		dmdPtr->aircraftTypeID = ac_conv(SEQ_POS,diPtr->smallestAllowableSeqPos,AC_TYPE_ID);
  		dmdPtr->sequencePosn = diPtr->smallestAllowableSeqPos;
		//RLZ: 07/02 Adjust for max upgrade
		if (diPtr->largestAllowableSeqPos > diPtr->smallestAllowableSeqPos)
			dmdPtr->maxUpgradeFromRequest = diPtr->largestAllowableSeqPos - diPtr->smallestAllowableSeqPos;
		else
			dmdPtr->maxUpgradeFromRequest = 0;


		// get associated ratios
		memset(&rBuf, '\0', sizeof(rBuf));
		rBuf.contractid = diPtr->contractid;
		tmp1 = TreeSearch(ratiosRoot, &rBuf, contractidcmp);
		if(! tmp1) {
			// didn't find contract id
			fprintf(logFile,"%s Line %d: couldn't find contractid %d for demand %d\n", __FILE__,__LINE__,diPtr->contractid, diPtr->demandid);
			sprintf(writetodbstring1, "%s Line %d: couldn't find contractid %d for demand %d", __FILE__,__LINE__,diPtr->contractid, diPtr->demandid);
			if(errorNumber==0)
	            {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
				       }
			    }
			else
			    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			    }
			    initializeWarningInfo(&errorinfoList[errorNumber]);
			    errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                strcpy(errorinfoList[errorNumber].group_name,"group_demand");
				errorinfoList[errorNumber].contractid=diPtr->contractid;
				errorinfoList[errorNumber].demandid=diPtr->demandid;
				sprintf(errorinfoList[errorNumber].filename, "%s", __FILE__);
				sprintf(errorinfoList[errorNumber].line_number, "%d", __LINE__);
			    errorinfoList[errorNumber].format_number=7;
                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				errorNumber++;
			    continue;
		}
		rPtr = (Ratios *) getTreeObject(tmp1);

		//
		// get flight_times for each ac type
		//

		// first get airport info
		if(!(outAptPtr = getAirportLatLonInfoByAptID(diPtr->outaptid))) {
			// didn't find airport id
			fprintf(logFile,"%s Line %d: couldn't find airportid %d for demand %d\n", __FILE__,__LINE__,diPtr->outaptid, diPtr->demandid);
			sprintf(writetodbstring1, "%s Line %d: couldn't find airportid %d for demand %d\n", __FILE__,__LINE__,diPtr->outaptid, diPtr->demandid);
            if(errorNumber==0)
	            {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
				       }
			    }
			else
			    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			    }
			    initializeWarningInfo(&errorinfoList[errorNumber]);
			    errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                strcpy(errorinfoList[errorNumber].group_name,"group_demand");
				errorinfoList[errorNumber].airportid=diPtr->outaptid;
				errorinfoList[errorNumber].demandid=diPtr->demandid;
				sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				sprintf(errorinfoList[errorNumber].line_number,"%d",__LINE__);
			    errorinfoList[errorNumber].format_number=8;
                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
			    continue;
		}
		if(!(inAptPtr = getAirportLatLonInfoByAptID(diPtr->inaptid))) {
			// didn't find airport id
			fprintf(logFile,"%s Line %d: couldn't find airportid %d for demand %d\n", __FILE__,__LINE__,diPtr->inaptid, diPtr->demandid);
			sprintf(writetodbstring1,"%s Line %d: couldn't find airportid %d for demand %d\n", __FILE__,__LINE__,diPtr->inaptid, diPtr->demandid);
            if(errorNumber==0)
	            {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
				       }
			    }
			else
			    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			    }
			   initializeWarningInfo(&errorinfoList[errorNumber]);
               errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
               strcpy(errorinfoList[errorNumber].group_name,"group_demand");
			   errorinfoList[errorNumber].airportid=diPtr->inaptid;
			   errorinfoList[errorNumber].demandid=diPtr->demandid;
			   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
			   sprintf(errorinfoList[errorNumber].line_number,"%d",__LINE__);
			   errorinfoList[errorNumber].format_number=8;
               strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
               errorNumber++;
			   continue;
		}
		dt_DateTimeToDateTimeParts(diPtr->dmd_outtime, &dtp);
		errNbr = 0;

		//START - After this part of the code, we will update the number of pax, if it is exceeding the capacity - 02/04/09 ANG
		//Note : * If it is an upgrade request, get the contract's actype. 
		//       * Other than that, take the fractionalprogramid in the demand table
		acTypeToCompare = diPtr->actypeid;
		if((kw = kwUpgradeRequest(diPtr->kwq))) {
			// upgrade requested, scheduled actype matches contract
			if(! (kwExists(KW_Upgrade_Approved, diPtr->kwq) || kwExists(KW_Guaranteed_Upgrade, diPtr->kwq))) {
				// upgrade has not been guaranteed or approved
				acTypeToCompare = diPtr->contract_actypeid;
			}
		}
		//END - 02/04/09 ANG

		for(i = 0; i < MAX_AC_TYPES; i++) {

			//START - Assume all trips in demand is within fleet capacity, therefore update number of pax if exceed max capacity - 02/04/09 ANG 
			if(acTypeToCompare == acTypeList[i].aircraftTypeID && diPtr->nbrPsgrs > acTypeList[i].capacity){
				//update information the corresponding demand's numpax info in the demand list
				for(j = 0; j < numDemand; j++){
					if(diPtr->demandid == demandList[j].demandID){
						demandList[j].numPax = acTypeList[i].capacity;
						fprintf(logFile, "Number of pax on demand %d is adjusted from %d to %d \n", diPtr->demandid, diPtr->nbrPsgrs, acTypeList[i].capacity);
						break;
					}
				}
				diPtr->nbrPsgrs = acTypeList[i].capacity;
			}
			//END - 02/04/09 ANG

			// this fails on too many passengers for ac type
			if(!(fcoPtr = doFlightCalc(outAptPtr->lat, outAptPtr->lon, inAptPtr->lat, inAptPtr->lon,
				(int) dtp.dparts.month, ac_conv(IDX_POS, i, AC_CD), diPtr->nbrPsgrs, &fcoBuf[i]))) {
				dmdPtr->incRevStatus[i] = -1;
				fcoBuf[i].blockTime = 0;
			}
		}
		if(verbose)
			fprintf(logFile,"| %6d | %4d | %4d | %4d | %4d | %4d |\n", diPtr->demandid,
				fcoBuf[0].blockTime, fcoBuf[1].blockTime, fcoBuf[2].blockTime, fcoBuf[3].blockTime, fcoBuf[4].blockTime);

		if(diPtr->contract_actypeid == ac_conv(SEQ_POS, diPtr->smallestAllowableSeqPos,AC_TYPE_ID) && !(upgradeKeywords(diPtr->kwq) || downgradeKeywords(diPtr->kwq))) {
			// If requested = owned:
			//  Owned incremental = 0;
			//  Upgrade incremental = R * (time in flown – time in owned) in hours  (should usually be <0)
			double R;
			i = ac_conv(AC_TYPE_ID, diPtr->contract_actypeid, IDX_POS);
			contractFcoPtr = &fcoBuf[i];
			R = getStandardRevenue(diPtr->contractid, diPtr->contract_actypeid);
			for(i++, fcoPtr = &fcoBuf[i]; i <  MAX_AC_TYPES; i++, ++fcoPtr) {
				if(dmdPtr->incRevStatus[i] != -1) {
					diPtr->incRev[i] = R * (((fcoPtr->blockTime - contractFcoPtr->blockTime) / 60.0));
					dmdPtr->incRev[i] = R * (((fcoPtr->blockTime - contractFcoPtr->blockTime) / 60.0));
				}
			}
			diPtr->logic = 1;
		}
		else if((kw = kwUpgradeRequest(diPtr->kwq)) && !(kwExists(KW_Guaranteed_Upgrade, diPtr->kwq) || kwExists(KW_Upgrade_Approved, diPtr->kwq))) {
//			If requested = upgrade:
//			  Owned incremental = 0;
//			  Upgrade incremental below requested type = R * (time_in_flown – time_in_owned) in hours  (usually < 0)
//			  Upgrade incremental at or above requested type = R * ratio_of_requested_plane * time_in_flown – R * time_in_owned + R * upgrade_bonus_pct
//			 (should be >0, at least for requested type; may become negative if we go much above requested type)
			double R;
			int ownIdx, reqIdx;
			R = getStandardRevenue(diPtr->contractid, diPtr->contract_actypeid);
			ownIdx = ac_conv(AC_TYPE_ID, diPtr->contract_actypeid, IDX_POS);
			contractFcoPtr = &fcoBuf[ownIdx];
			reqIdx = ac_conv(UPGRADE, kw, IDX_POS);
			requestedFcoPtr = &fcoBuf[reqIdx];
			i = ownIdx + 1;
			for(fcoPtr = &fcoBuf[i]; i < reqIdx; ++i, ++fcoPtr) {
				if(dmdPtr->incRevStatus[i] != -1) {
					diPtr->incRev[i] = R * (((fcoPtr->blockTime - contractFcoPtr->blockTime) / 60.0));
					dmdPtr->incRev[i] = R * (((fcoPtr->blockTime - contractFcoPtr->blockTime) / 60.0));
				}
			}
			for(; i < MAX_AC_TYPES; i++, ++fcoPtr) {
				if(dmdPtr->incRevStatus[i] != -1) {
					diPtr->incRev[i] = R * rPtr->ratios[reqIdx] * (fcoPtr->blockTime  / 60.0) - R * (contractFcoPtr->blockTime / 60.0) + R * BONUS;
					dmdPtr->incRev[i] = R * rPtr->ratios[reqIdx] * (fcoPtr->blockTime  / 60.0) - R * (contractFcoPtr->blockTime / 60.0) + R * BONUS;
				}

			}
			diPtr->logic = 2;
		}
// same as guaranteed/approved downgrade logic
//		else if(kwExists(KW_Guaranteed_Upgrade, diPtr->kwq) || kwExists(KW_Upgrade_Approved, diPtr->kwq)) {
//			double R;
//			R = getStandardRevenue(diPtr->contractid, diPtr->contract_actypeid);
//			i = ac_conv(AC_TYPE_ID, diPtr->contract_actypeid, IDX_POS);
//			contractFcoPtr = &fcoBuf[i];
//			i = ac_conv(AC_TYPE_ID, diPtr->actypeid, IDX_POS);
//			fcoPtr = &fcoBuf[i];
//			for(++i, ++fcoPtr; i < MAX_AC_TYPES; ++i, ++fcoPtr) {
//				diPtr->incRev[i] = R * rPtr->ratios[i] * (fcoPtr->blockTime  / 60.0) - R * (contractFcoPtr->blockTime / 60.0) + R * BONUS;
//
//			}
//			diPtr->logic = 3;
//		}
		else if((kw = kwDowngradeRequest(diPtr->kwq)) && !(kwExists(KW_Guaranteed_Downgrade, diPtr->kwq) || kwExists(KW_Downgrade_Approved,diPtr->kwq))) {
//			If downgrade request (not approved or guaranteed)
//			  requested plane flown: incremental = 0
//			  above requested but below owned: incremental = R * ratio_of_requested_plane * (time_in_flown - time_in_requested)
//			  owned or above: incremental = R * time_in_flown - R * ratio_of_requested_plane * time_in_requested
			double R;
			int reqIdx;
			R = getStandardRevenue(diPtr->contractid, diPtr->contract_actypeid);
			reqIdx = ac_conv(DOWNGRADE, kw, IDX_POS);
			requestedFcoPtr = &fcoBuf[reqIdx];
			i = reqIdx + 1;;
			fcoPtr = &fcoBuf[i];
			for(; i < ac_conv(AC_TYPE_ID,diPtr->contract_actypeid,IDX_POS); i++, fcoPtr++) {
				if(dmdPtr->incRevStatus[i] != -1) {
					diPtr->incRev[i] = R * rPtr->ratios[reqIdx] * ((fcoPtr->blockTime - requestedFcoPtr->blockTime) / 60.0);
					dmdPtr->incRev[i] = R * rPtr->ratios[reqIdx] * ((fcoPtr->blockTime - requestedFcoPtr->blockTime) / 60.0);
				}
			}
			for(; i < MAX_AC_TYPES; i++, ++fcoPtr) {
				if(dmdPtr->incRevStatus[i] != -1) {
					diPtr->incRev[i] = R * (fcoPtr->blockTime / 60.0) - R * rPtr->ratios[reqIdx] * (requestedFcoPtr->blockTime / 60.0);
					dmdPtr->incRev[i] = R * (fcoPtr->blockTime / 60.0) - R * rPtr->ratios[reqIdx] * (requestedFcoPtr->blockTime / 60.0);
				}
			}
			diPtr->logic = 4;

		}
		else if(kwExists(KW_Guaranteed_Downgrade, diPtr->kwq) || kwExists(KW_Downgrade_Approved, diPtr->kwq)
			|| kwExists(KW_Guaranteed_Upgrade, diPtr->kwq) || kwExists(KW_Upgrade_Approved, diPtr->kwq)) {
//			If guaranteed/approved downgrade/upgrade
//			  guaranteed/approved plane flown: incremental = 0
//			  for each plane above that: incremental = R * ratio_of_guaranteed_approved_plane * (time_in_flown - time_in_guaranteed)
			double R;
			int reqIdx;
			R = getStandardRevenue(diPtr->contractid, diPtr->contract_actypeid);
			reqIdx = ac_conv(SEQ_POS, diPtr->smallestAllowableSeqPos,IDX_POS);
			requestedFcoPtr = &fcoBuf[reqIdx];
			i = reqIdx + 1;
			fcoPtr = &fcoBuf[i];
			for(; i < MAX_AC_TYPES; i++, ++fcoPtr) {
				if(dmdPtr->incRevStatus[i] != -1) {
					diPtr->incRev[i] = R * rPtr->ratios[reqIdx] * ((fcoPtr->blockTime - requestedFcoPtr->blockTime) / 60.0);
					dmdPtr->incRev[i] = R * rPtr->ratios[reqIdx] * ((fcoPtr->blockTime - requestedFcoPtr->blockTime) / 60.0);
				}
			}
			if(kwExists(KW_Guaranteed_Upgrade, diPtr->kwq) || kwExists(KW_Upgrade_Approved, diPtr->kwq))
				diPtr->logic = 3;
			else
				diPtr->logic = 5;
		}

	}
	if(verbose)
		fprintf(logFile,"+--------+------+------+------+------+------+\n");
	if(!verbose)
		return;

	logMsg(logFile,"\n\nDemand KeyWord Information:\n");
	fprintf(logFile,"Notes:\n");
	fprintf(logFile,"*1) currently scheduled upgrade wasn't requested\n");
	fprintf(logFile,"*2) currently scheduled downgrade wasn't requested\n");
	fprintf(logFile,"*3) approved or guaranteed upgrade is not currently scheduled\n");
	fprintf(logFile,"*4) scheduled guaranteed or approved upgrade but not to what was requested\n");
	fprintf(logFile,"*5) downgrade was requested, upgrade is currently scheduled\n");
	fprintf(logFile,"*6) currently scheduled downgrade is lower than requested downgrade\n");
	fprintf(logFile,"*7) upgrade was requested, downgrade is currently scheduled\n");
	fprintf(logFile,"*8) ambiguous keywords. defaulting to contract plane.\n");
	fprintf(logFile,"\nN/A where no incremental revenue calculation is performed because the aircraft type does not support the number of passengers that will be flying.\n\n");
	fprintf(logFile,"+---+-------+-------+-------+-------+--------+--------------+---------+---------+------------------+------------------+---------+---------+----------+-----+-----+------------+-----+-----+------------+\n");
	fprintf(logFile,"| L |       |       |       |       |        |              |         |         |                  |                  |         |         |          | CT  | CT  | Contract   |     |     |            |\n");
	fprintf(logFile,"| o |       |       |       |       |        | Smallest     |         | Cntract |                  |                  |  Demand |   Other | Short    | Seq | AC  | Aircraft   | Seq | AC  | Aircraft   |\n");
	fprintf(logFile,"| g |   CJ1 |  Brav |   CJ3 |   Exc |    Sov | Aircraft     | OwnerId | Id      | Demand Out Time  | Out Time         |      Id |      Id | Name     | Pos | TYP | Name       | Pos | TYP | Name       | Keywords\n");
	fprintf(logFile,"+---+-------+-------+-------+-------+--------+--------------+---------+---------+------------------+------------------+---------+---------+----------+-----+-----+------------+-----+-----+------------+\n");

	for(tmp = Minimum(dmdKeyWdRoot); tmp; tmp = Successor(tmp)) {
		diPtr = (DemandInfo *) getTreeObject(tmp);
		dmdBuf.demandID = diPtr->demandid;
		tmp2 = TreeSearch(dmdXDemandIdRoot,&dmdBuf,demandIdCompare);
		if(! tmp2) {
			// didn't find this demandid.
			// not in planning window
			continue;
		}
		dmdPtr = (Demand *) getTreeObject(tmp2);

		fprintf(logFile,"| %d ", diPtr->logic);

		for(i = 0; i < (MAX_AC_TYPES - 1); i++) {
			if(dmdPtr->incRevStatus[i] == -1)
				fprintf(logFile,"|  N/A  ");
			else
				fprintf(logFile,"| %5.0f ", diPtr->incRev[i]);
		}
		// do sovereign here
		if(dmdPtr->incRevStatus[i] == -1)
			fprintf(logFile,"|    N/A ");
		else
			fprintf(logFile,"| %6.0f ", diPtr->incRev[i]);

		if(diPtr->smallestAllowableIssue)
			sprintf(tbuf,"*%d", diPtr->smallestAllowableIssue);
		else
			strcpy(tbuf,"  ");
		fprintf(logFile,"| %10s%2s ", getsp_text(diPtr->smallestAllowableSeqPos), tbuf);

		strcpy(tbuf2, getsp_text(diPtr->contract_seqpos));
		strcpy(tbuf3, getsp_text(diPtr->seqpos));

		fprintf(logFile,"| %7d | %7d | %16s | %16s | %7d | %7d | %-8s | %3d | %3d | %-10s | %3d | %3d | %-10s | ",
			diPtr->ownerid,
			diPtr->contractid,
			(diPtr->dmd_outtime) ? dt_DateTimeToDateTimeString(diPtr->dmd_outtime,tbuf,"%Y/%m/%d %H:%M") : "",
			(diPtr->outtime) ? dt_DateTimeToDateTimeString(diPtr->outtime,tbuf1,"%Y/%m/%d %H:%M") : "",
			diPtr->demandid,
			diPtr->otherid,
			diPtr->shortname,
			diPtr->contract_seqpos,
			diPtr->contract_actypeid,
			tbuf2,
			diPtr->seqpos,
			diPtr->actypeid,
			tbuf3);
		count = QGetCount(diPtr->kwq);
		for(i = 0, qi = QGetHead(diPtr->kwq); i < count; ++i, qi = QGetNext(diPtr->kwq, qi)) {
			kwRecPtr =  (KW_Rec *)QGetObject(qi);
			fprintf(logFile,"%s, ", getKW_text(kwRecPtr->kw));
		}
		fprintf(logFile,"\n");
	}
	fprintf(logFile,"+---+-------+-------+-------+-------+--------+--------------+---------+---------+------------------+------------------+---------+---------+----------+-----+-----+------------+-----+-----+------------+\n");
}


static void
getUpgradeDowngradeData(MY_CONNECTION *myconn)
{

	//char *upgradeDowngradeSQL = "select ud.*, dmd.outaptid, dmd.inaptid, dmd.numberofpassengers from upgradedowngrade ud, demand dmd where dmd.demandid = ud.demandid";
	char *upgradeDowngradeSQL = "select ud.*, dmd.outaptid, dmd.inaptid, dmd.numberofpassengers from upgradedowngrade ud, demand dmd where dmd.demandid = ud.demandid and legkeywordid != 0";
	//maxDemandID: only care about the demand in the planning window range. RLZ 10/22/2007
	char *ratiosSQL = "select * from ratios";

	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	int errNbr = 0;


	DemandInfo *diPtr, diBuf;
	KeyWord kw;
	keyword_rec_type kwrt;
	KW_Rec *kwRecPtr, kwRecBuf;
	int demandid;
	BINTREENODE *tmp;
	Ratios *rPtr, rBuf;
	char writetodbstring1[200];

	if(!myDoQuery(myconn, ratiosSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"%s Line %d: readUpgradeDowngrade(): no results.\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"%s Line %d: readUpgradeDowngrade(): 0 rows returned.\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
#ifdef DEBUGGING
	logMsg(logFile,"raw query results for ratiosSQL:\n");
	if (verbose) displayResults(res, cols);
	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
#endif // DEBUGGING

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		memset(&rBuf, '\0', sizeof(rBuf));
		rBuf.contractid = atoi(row[rt_contractid]);
		tmp = TreeSearch(ratiosRoot, &rBuf, contractidcmp);
		if(tmp) {
			// contractid already exists in tree
			rPtr = (Ratios *) getTreeObject(tmp);
			rPtr->ratios[ac_conv(AC_TYPE_ID, atoi(row[rt_actypeid]), IDX_POS)] = atof(row[rt_ratio]);
			continue;
		}

		rPtr = calloc((size_t) 1, sizeof(Ratios));

		if(! rPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		rPtr->contractid = atoi(row[rt_contractid]);
		rPtr->contract_actypeid = atoi(row[rt_contract_actypeid]);
		rPtr->actypeid = atoi(row[rt_actypeid]);
		rPtr->ratios[ac_conv(AC_TYPE_ID, rPtr->actypeid, IDX_POS)] = atof(row[rt_ratio]);
		if(!(ratiosRoot = RBTreeInsert(ratiosRoot, rPtr, contractidcmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readUpgradeDowngrade().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}

	// free mysql results
	mysql_free_result(res);

	if(!myDoQuery(myconn, upgradeDowngradeSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"readUpgradeDowngrade(): no results.\n");
		writeWarningData(myconn); exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readUpgradeDowngrade(): 0 rows returned.\n");
		writeWarningData(myconn); exit(1);
	}
#ifdef DEBUGGING
	logMsg(logFile,"raw query results for upgradeDowngradeSQL:\n");
	if (verbose) displayResults(res, cols);
	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
#endif // DEBUGGING

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;

		if(strcmp(row[ud_type], "demand") == 0)
			kwrt = kw_demand;
		else if(strcmp(row[ud_type], "mgdleg") == 0)
			kwrt = kw_mgdleg;
		else if(strcmp(row[ud_type], "lpgleg") == 0)
			kwrt = kw_lpgleg;
		else {
			logMsg(logFile,"%s Line %d, unexpected value for \"type\" in readUpgradeDowngrade().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(row[ud_legkeywordid])
			kw = atoi(row[ud_legkeywordid]);

		/*
			first test for DemandInfo already existing in tree for demandid
		*/

		if(row[ud_demandid]) {
			if(!(demandid = atoi(row[ud_demandid]))) {
				logMsg(logFile,"%s Line %d, zero demandid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
				sprintf(writetodbstring1, "%s Line %d, zero demandid in readUpgradeDowngrade().",__FILE__,__LINE__);
				if(errorNumber==0)
	              {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in getUpgradeDowngradeData().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
				       }
			      }
			    else
			      {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in getUpgradeDowngradeData().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			      }
				  initializeWarningInfo(&errorinfoList[errorNumber]);
				  errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                  strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				  sprintf(errorinfoList[errorNumber].filename, "%s",  __FILE__);
				  sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			      errorinfoList[errorNumber].format_number=25;
                  strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				  errorNumber++;
				  writeWarningData(myconn); exit(1);
			}
		}
		else {
			logMsg(logFile,"%s Line %d, null demandid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
			sprintf(writetodbstring1, "%s Line %d, null demandid in readUpgradeDowngrade().",__FILE__,__LINE__);
			if(errorNumber==0)
	              {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in getUpgradeDowngradeData().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
				       }
			      }
			else
			      {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in getUpgradeDowngradeData().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			      }
			      initializeWarningInfo(&errorinfoList[errorNumber]);
				  errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                  strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				  sprintf(errorinfoList[errorNumber].filename, "%s",  __FILE__);
				  sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			      errorinfoList[errorNumber].format_number=25;
                  strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				  errorNumber++;
			      writeWarningData(myconn); exit(1);
		}

		memset(&diBuf, '\0', sizeof(diBuf));
		diBuf.demandid = demandid;
		tmp = TreeSearch(dmdKeyWdRoot, &diBuf, demandidcmp);
		if(tmp) {
			// demandid already exists in tree
			diPtr = (DemandInfo *) getTreeObject(tmp);
			kwRecBuf.kwrt = kwrt;
			kwRecBuf.kw = kw;
			if(! kwExists(kw, diPtr->kwq)) {
				// new keyword
				if(! (kwRecPtr = (KW_Rec *) calloc((size_t) 1, sizeof(KW_Rec)))) {
					logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
					writeWarningData(myconn); exit(1);
				}
				kwRecPtr->kwrt = kwrt;
				kwRecPtr->kw = kw;
				(void *) QAddToTail(diPtr->kwq, kwRecPtr);
			}
			continue;
		}
		else {
			// new demandid
			diPtr = calloc((size_t) 1, sizeof(DemandInfo));

			if(! diPtr) {
				logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			diPtr->kwq = createQueue();
			if(! (kwRecPtr = calloc((size_t) 1, sizeof(KW_Rec)))) {
				logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			kwRecPtr->kwrt = kwrt;
			kwRecPtr->kw = kw;
			(void *) QAddToTail(diPtr->kwq, kwRecPtr);
		}

		if(row[ud_ownerid]) {
			if(!(diPtr->ownerid = atoi(row[ud_ownerid]))) {
				logMsg(logFile,"%s Line %d, zero ownerid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[ud_contractid]) {
			if(!(diPtr->contractid = atoi(row[ud_contractid]))) {
				logMsg(logFile,"%s Line %d, zero contractid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[ud_dmd_outtime]) {
			if((diPtr->dmd_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[ud_dmd_outtime], NULL, &errNbr)) == BadDateTime) {
				logMsg(logFile,"%s Line %d, Bad date in readUpgradeDowngrade(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[ud_dmd_outtime]);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[ud_outtime]) {
			if((diPtr->outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[ud_outtime], NULL, &errNbr)) == BadDateTime) {
				logMsg(logFile,"%s Line %d, Bad date in readUpgradeDowngrade(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[ud_outtime]);
				writeWarningData(myconn); exit(1);
			}
		}

		diPtr->demandid = demandid;

		if(row[ud_otherid]) {
			if(!(diPtr->otherid = atoi(row[ud_otherid]))) {
				logMsg(logFile,"%s Line %d, zero otherid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(!(diPtr->shortname = strdup(row[ud_shortname]))) {
			logMsg(logFile,"%s Line %d, Out of Memory in readUpgradeDowngrade().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(row[ud_contract_seqpos]) {
			if(!(diPtr->contract_seqpos = atoi(row[ud_contract_seqpos]))) {
				logMsg(logFile,"%s Line %d, zero contract_seqpos in readUpgradeDowngrade().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[ud_contract_actypeid]) {
			if(!(diPtr->contract_actypeid = atoi(row[ud_contract_actypeid]))) {
				logMsg(logFile,"%s Line %d, zero contract_actypeid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[ud_seqpos]) {
			if(!(diPtr->seqpos = atoi(row[ud_seqpos]))) {
				logMsg(logFile,"%s Line %d, zero seqpos in readUpgradeDowngrade().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[ud_actypeid]) {
			if(!(diPtr->actypeid = atoi(row[ud_actypeid]))) {
				logMsg(logFile,"%s Line %d, zero actypeid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[ud_outaptid]) {
			if(!(diPtr->outaptid = atoi(row[ud_outaptid]))) {
				logMsg(logFile,"%s Line %d, zero outaptid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[ud_inaptid]) {
			if(!(diPtr->inaptid = atoi(row[ud_inaptid]))) {
				logMsg(logFile,"%s Line %d, zero inaptid in readUpgradeDowngrade().\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(row[ud_nbrPsgrs])
			diPtr->nbrPsgrs = atoi(row[ud_nbrPsgrs]);
		if(!(dmdKeyWdRoot = RBTreeInsert(dmdKeyWdRoot, diPtr, demandidcmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readUpgradeDowngrade().\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		//RLZ: initialize the largestAllowableSeqPos
		diPtr->largestAllowableSeqPos = diPtr->contract_seqpos;
	}

	// free mysql results
	mysql_free_result(res);



}
/*****************************************************************************************
*	Function   getUpgradeDowngradeDataFromOracle()
*	Purpose:  read in upgrades and downgrades data pulled from Oracle
*****************************************************************************************/
static void
getUpgradeDowngradeDataFromOracle(void)
{
	int wc;
	char *wptrs[64];
	BINTREENODE *tmp;
	FILE *inp;
	char buf[1024];

	int errNbr = 0;


	DemandInfo *diPtr, diBuf;
	KeyWord kw;
	keyword_rec_type kwrt;
	KW_Rec *kwRecPtr, kwRecBuf;
	int demandid;
	Ratios *rPtr, rBuf;

	if(!(inp = fopen("oracle/ratios.txt", "r"))) {
		logMsg(logFile,"%s Line %d, can't open \"oracle/ratios.txt\"",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	while(fgets(buf, sizeof(buf) -1, inp)) {
		wc = splitA(buf, "\t\r\n", wptrs);

		memset(&rBuf, '\0', sizeof(rBuf));
		rBuf.contractid = atoi(wptrs[rt_contractid]);
		tmp = TreeSearch(ratiosRoot, &rBuf, contractidcmp);
		if(tmp) {
			// contractid already exists in tree
			rPtr = (Ratios *) getTreeObject(tmp);
			rPtr->ratios[ac_conv(AC_TYPE_ID, atoi(wptrs[rt_actypeid]), IDX_POS)] = atof(wptrs[rt_ratio]);
			continue;
		}

		rPtr = calloc((size_t) 1, sizeof(Ratios));

		if(! rPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		rPtr->contractid = atoi(wptrs[rt_contractid]);
		rPtr->contract_actypeid = atoi(wptrs[rt_contract_actypeid]);
		rPtr->actypeid = atoi(wptrs[rt_actypeid]);
		rPtr->ratios[ac_conv(AC_TYPE_ID, rPtr->actypeid, IDX_POS)] = atof(wptrs[rt_ratio]);
		if(!(ratiosRoot = RBTreeInsert(ratiosRoot, rPtr, contractidcmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed.\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}

	fclose(inp);

	if(!(inp = fopen("oracle/upgradedowngrade.txt", "r"))) {
		logMsg(logFile,"%s Line %d, can't open \"oracle/upgradedowngrade.txt\"",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	while(fgets(buf, sizeof(buf) -1, inp)) {
		wc = splitA(buf, "\t\r\n", wptrs);

		if(strcmp(wptrs[ud_type], "demand") == 0)
			kwrt = kw_demand;
		else if(strcmp(wptrs[ud_type], "mgdleg") == 0)
			kwrt = kw_mgdleg;
		else if(strcmp(wptrs[ud_type], "lpgleg") == 0)
			kwrt = kw_lpgleg;
		else {
			logMsg(logFile,"%s Line %d, unexpected value for \"type\".\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(wptrs[ud_legkeywordid][0])
			kw = atoi(wptrs[ud_legkeywordid]);

		
			//first test for DemandInfo already existing in tree for demandid
		

		if(wptrs[ud_demandid][0]) {
			if(!(demandid = atoi(wptrs[ud_demandid]))) {
				logMsg(logFile,"%s Line %d, zero demandid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		else {
			logMsg(logFile,"%s Line %d, null demandid.\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		memset(&diBuf, '\0', sizeof(diBuf));
		diBuf.demandid = demandid;
		tmp = TreeSearch(dmdKeyWdRoot, &diBuf, demandidcmp);
		if(tmp) {
			// demandid already exists in tree
			diPtr = (DemandInfo *) getTreeObject(tmp);
			kwRecBuf.kwrt = kwrt;
			kwRecBuf.kw = kw;
			if(! kwExists(kw, diPtr->kwq)) {
				// new keyword
				if(! (kwRecPtr = (KW_Rec *) calloc((size_t) 1, sizeof(KW_Rec)))) {
					logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
					writeWarningData(myconn); exit(1);
				}
				kwRecPtr->kwrt = kwrt;
				kwRecPtr->kw = kw;
				(void *) QAddToTail(diPtr->kwq, kwRecPtr);
			}
			continue;
		}
		else {
			// new demandid   -- Can we assume this will never happen? 
			diPtr = calloc((size_t) 1, sizeof(DemandInfo));

			if(! diPtr) {
				logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			diPtr->kwq = createQueue();
			if(! (kwRecPtr = calloc((size_t) 1, sizeof(KW_Rec)))) {
				logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			kwRecPtr->kwrt = kwrt;
			kwRecPtr->kw = kw;  //kw NOT defined? because if(wptrs[ud_legkeywordid][0])
			(void *) QAddToTail(diPtr->kwq, kwRecPtr);
		}

		if(wptrs[ud_ownerid][0]) {
			if(!(diPtr->ownerid = atoi(wptrs[ud_ownerid]))) {
				logMsg(logFile,"%s Line %d, zero ownerid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(wptrs[ud_contractid][0]) {
			if(!(diPtr->contractid = atoi(wptrs[ud_contractid]))) {
				logMsg(logFile,"%s Line %d, zero contractid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(wptrs[ud_dmd_outtime][0]) {
			if((diPtr->dmd_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", wptrs[ud_dmd_outtime], NULL, &errNbr)) == BadDateTime) {
				logMsg(logFile,"%s Line %d, Bad date. errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, wptrs[ud_dmd_outtime]);
				writeWarningData(myconn); exit(1);
			}
		}
		if(wptrs[ud_outtime][0]) {
			if((diPtr->outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", wptrs[ud_outtime], NULL, &errNbr)) == BadDateTime) {
				logMsg(logFile,"%s Line %d, Bad date. errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, wptrs[ud_outtime]);
				writeWarningData(myconn); exit(1);
			}
		}

		diPtr->demandid = demandid;

		if(wptrs[ud_otherid][0]) {
			if(!(diPtr->otherid = atoi(wptrs[ud_otherid]))) {
				logMsg(logFile,"%s Line %d, zero otherid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(!(diPtr->shortname = strdup(wptrs[ud_shortname]))) {
			logMsg(logFile,"%s Line %d, Out of Memory.\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(wptrs[ud_contract_seqpos][0]) {
			if(!(diPtr->contract_seqpos = atoi(wptrs[ud_contract_seqpos]))) {
				logMsg(logFile,"%s Line %d, zero contract_seqpos.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(wptrs[ud_contract_actypeid][0]) {
			if(!(diPtr->contract_actypeid = atoi(wptrs[ud_contract_actypeid]))) {
				logMsg(logFile,"%s Line %d, zero contract_actypeid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(wptrs[ud_seqpos][0]) {
			if(!(diPtr->seqpos = atoi(wptrs[ud_seqpos]))) {
				logMsg(logFile,"%s Line %d, zero seqpos.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(wptrs[ud_actypeid][0]) {
			if(!(diPtr->actypeid = atoi(wptrs[ud_actypeid]))) {
				logMsg(logFile,"%s Line %d, zero actypeid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(wptrs[ud_outaptid][0]) {
			if(!(diPtr->outaptid = atoi(wptrs[ud_outaptid]))) {
				logMsg(logFile,"%s Line %d, zero outaptid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(wptrs[ud_inaptid][0]) {
			if(!(diPtr->inaptid = atoi(wptrs[ud_inaptid]))) {
				logMsg(logFile,"%s Line %d, zero inaptid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(wptrs[ud_nbrPsgrs][0])
			diPtr->nbrPsgrs = atoi(wptrs[ud_nbrPsgrs]);
		if(!(dmdKeyWdRoot = RBTreeInsert(dmdKeyWdRoot, diPtr, demandidcmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed.\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		//RLZ: initialize the largestAllowableSeqPos
		diPtr->largestAllowableSeqPos = diPtr->contract_seqpos;
	}
	fclose(inp);
	

}

/*****************************************************************************************
*	Function   getUpgradeDowngradeDataFromOracleDB()
*	Purpose:  read in upgrades and downgrades data directly from Oracle
*****************************************************************************************/
static void
getUpgradeDowngradeDataFromOracleDB(ORACLE_SOCKET *orl_socket)
{
	
	BINTREENODE *tmp;
	char buf[1024];

	int errNbr = 0;


	DemandInfo *diPtr, diBuf;
	KeyWord kw;
	keyword_rec_type kwrt;
	KW_Rec *kwRecPtr, kwRecBuf;
	int demandid;
	Ratios *rPtr, rBuf;

	extern char *ratioOracleSQL;
	extern char *upgradeDowngradeOracleSQL;

	//test
	int test;
	//

	 if(Orlconnection_doquery(orl_socket, ratioOracleSQL))
	 { logMsg(logFile,"%s Line %d: failed to execute query and get data\n", __FILE__,__LINE__);
	   exit(1);
	 }
	 while(Orlconnection_fetch(orl_socket)==0){
        memset(&rBuf, '\0', sizeof(rBuf));
		rBuf.contractid = atoi(orl_socket->results[rt_contractid]);
		tmp = TreeSearch(ratiosRoot, &rBuf, contractidcmp);
		if(tmp) {
			// contractid already exists in tree
			rPtr = (Ratios *) getTreeObject(tmp);
			rPtr->ratios[ac_conv(AC_TYPE_ID, atoi(orl_socket->results[rt_actypeid]), IDX_POS)] = atof(orl_socket->results[rt_ratio]);
			continue;
		}

		rPtr = calloc((size_t) 1, sizeof(Ratios));

		if(! rPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		rPtr->contractid = atoi(orl_socket->results[rt_contractid]);
		rPtr->contract_actypeid = atoi(orl_socket->results[rt_contract_actypeid]);
		rPtr->actypeid = atoi(orl_socket->results[rt_actypeid]);
		rPtr->ratios[ac_conv(AC_TYPE_ID, rPtr->actypeid, IDX_POS)] = atof(orl_socket->results[rt_ratio]);
		if(!(ratiosRoot = RBTreeInsert(ratiosRoot, rPtr, contractidcmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed.\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	 }

	 if(Orlconnection_doquery(orl_socket, upgradeDowngradeOracleSQL))
	 { logMsg(logFile,"%s Line %d: failed to execute query and get data\n", __FILE__,__LINE__);
	   exit(1);
	 }

	 

	 while(Orlconnection_fetch(orl_socket)==0){

		if(strcmp(orl_socket->results[ud_type], "demand") == 0)
			kwrt = kw_demand;
		else if(strcmp(orl_socket->results[ud_type], "mgdleg") == 0)
			kwrt = kw_mgdleg;
		else if(strcmp(orl_socket->results[ud_type], "lpgleg") == 0)
			kwrt = kw_lpgleg;
		else {
			logMsg(logFile,"%s Line %d, unexpected value for \"type\".\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(orl_socket->results[ud_legkeywordid][0])
			kw = atoi(orl_socket->results[ud_legkeywordid]);

		/*
			first test for DemandInfo already existing in tree for demandid
		*/

		if(orl_socket->results[ud_demandid][0]) {
			if(!(demandid = atoi(orl_socket->results[ud_demandid]))) {
				logMsg(logFile,"%s Line %d, zero demandid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		else {
			logMsg(logFile,"%s Line %d, null demandid.\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		memset(&diBuf, '\0', sizeof(diBuf));
		diBuf.demandid = demandid;
		tmp = TreeSearch(dmdKeyWdRoot, &diBuf, demandidcmp);
		if(tmp) {
			// demandid already exists in tree
			diPtr = (DemandInfo *) getTreeObject(tmp);
			kwRecBuf.kwrt = kwrt;
			kwRecBuf.kw = kw;
			if(! kwExists(kw, diPtr->kwq)) {
				// new keyword
				if(! (kwRecPtr = (KW_Rec *) calloc((size_t) 1, sizeof(KW_Rec)))) {
					logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
					writeWarningData(myconn); exit(1);
				}
				kwRecPtr->kwrt = kwrt;
				kwRecPtr->kw = kw;
				(void *) QAddToTail(diPtr->kwq, kwRecPtr);
			}
			continue;
		}
		else {
			// new demandid   -- Can we assume this will never happen? 
			diPtr = calloc((size_t) 1, sizeof(DemandInfo));

			if(! diPtr) {
				logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			diPtr->kwq = createQueue();
			if(! (kwRecPtr = calloc((size_t) 1, sizeof(KW_Rec)))) {
				logMsg(logFile,"%s Line %d, Out of Memory.\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
			kwRecPtr->kwrt = kwrt;
			kwRecPtr->kw = kw;  //kw NOT defined? because if(wptrs[ud_legkeywordid][0])
			(void *) QAddToTail(diPtr->kwq, kwRecPtr);
		}

		if(strlen(orl_socket->results[ud_ownerid])) {
			if(!(diPtr->ownerid = atoi(orl_socket->results[ud_ownerid]))) {
				logMsg(logFile,"%s Line %d, zero ownerid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(strlen(orl_socket->results[ud_contractid])) {
			if(!(diPtr->contractid = atoi(orl_socket->results[ud_contractid]))) {
				logMsg(logFile,"%s Line %d, zero contractid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(strlen(orl_socket->results[ud_dmd_outtime])) {
			if((diPtr->dmd_outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket->results[ud_dmd_outtime], NULL, &errNbr)) == BadDateTime) {
				logMsg(logFile,"%s Line %d, Bad date. errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, orl_socket->results[ud_dmd_outtime]);
				writeWarningData(myconn); exit(1);
			}
		}
        
		//test = atoi(orl_socket->results[ud_dmd_outtime]);

		if(strlen(orl_socket->results[ud_outtime])) {
			if((diPtr->outtime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket->results[ud_outtime], NULL, &errNbr)) == BadDateTime) {
				logMsg(logFile,"%s Line %d, Bad date. errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, orl_socket->results[ud_outtime]);
				writeWarningData(myconn); exit(1);
			}
		}

		diPtr->demandid = demandid;

		if(strlen(orl_socket->results[ud_otherid])) {
			if(!(diPtr->otherid = atoi(orl_socket->results[ud_otherid]))) {
				logMsg(logFile,"%s Line %d, zero otherid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(!(diPtr->shortname = strdup(orl_socket->results[ud_shortname]))) {
			logMsg(logFile,"%s Line %d, Out of Memory.\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if(strlen(orl_socket->results[ud_contract_seqpos])) {
			if(!(diPtr->contract_seqpos = atoi(orl_socket->results[ud_contract_seqpos]))) {
				logMsg(logFile,"%s Line %d, zero contract_seqpos.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(strlen(orl_socket->results[ud_contract_actypeid])) {
			if(!(diPtr->contract_actypeid = atoi(orl_socket->results[ud_contract_actypeid]))) {
				logMsg(logFile,"%s Line %d, zero contract_actypeid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(strlen(orl_socket->results[ud_seqpos])) {
			if(!(diPtr->seqpos = atoi(orl_socket->results[ud_seqpos]))) {
				logMsg(logFile,"%s Line %d, zero seqpos.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(strlen(orl_socket->results[ud_actypeid])) {
			if(!(diPtr->actypeid = atoi(orl_socket->results[ud_actypeid]))) {
				logMsg(logFile,"%s Line %d, zero actypeid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(strlen(orl_socket->results[ud_outaptid])) {
			if(!(diPtr->outaptid = atoi(orl_socket->results[ud_outaptid]))) {
				logMsg(logFile,"%s Line %d, zero outaptid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(strlen(orl_socket->results[ud_inaptid])) {
			if(!(diPtr->inaptid = atoi(orl_socket->results[ud_inaptid]))) {
				logMsg(logFile,"%s Line %d, zero inaptid.\n",__FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
		if(strlen(orl_socket->results[ud_nbrPsgrs]))
			diPtr->nbrPsgrs = atoi(orl_socket->results[ud_nbrPsgrs]);
		if(!(dmdKeyWdRoot = RBTreeInsert(dmdKeyWdRoot, diPtr, demandidcmp))) {
			logMsg(logFile,"%s Line %d, RBTreeInsert() failed.\n",__FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		//RLZ: initialize the largestAllowableSeqPos
		diPtr->largestAllowableSeqPos = diPtr->contract_seqpos;
	}
	

}

static int
demandidcmp(void *a1, void *b1)
{
	DemandInfo *a = (DemandInfo *) a1;
	DemandInfo *b = (DemandInfo *) b1;

	return(a->demandid - b->demandid);
}

static int
contractidcmp(void *a1, void *b1)
{
	Ratios *a = (Ratios *) a1;
	Ratios *b = (Ratios *) b1;

	return(a->contractid - b->contractid);
}

static char *
getKW_text(KeyWord kw)
{
	KW_Text *kwtPtr = &kt[0];

	while(kwtPtr->text) {
		if(kwtPtr->kw == kw)
			return(kwtPtr->text);
		++kwtPtr;
	}
	return("KeyWord not found,");
}

static char *
getsp_text(int sp)
{
	SP_Text *spPtr = &spt[0];

	while(spPtr->text) {
		if(spPtr->seqpos == sp)
			return(spPtr->text);
		++spPtr;
	}
	return("seqpos not found,");
}

// returns true if keyword in list, false otherwise
static int
kwExists(KeyWord kw, QLIST *q)
{
	QITEM *qi;
	int i;
	int count =  QGetCount(q);
	KW_Rec *kwRecPtr;

        for(i = 0, qi = QGetHead(q); i < count; ++i, qi = QGetNext(q, qi)) {
                kwRecPtr = (KW_Rec *) QGetObject(qi);
		if(kw == kwRecPtr->kw)
			return(1);
        }
	return(0);
}

// returns true if smaller sequence_Position is scheduled than the contract specifies.
// will return true if smaller sequence_position is scheduled even in the absense of downgrade request,
// downgrade approved, or guar. downgrade keywords.
static int
kwDowngrade(DemandInfo *diPtr)
{
	if(diPtr->contract_seqpos > diPtr->seqpos)
		return(1);
	return(0);
}

// returns true if larger sequence_Position is scheduled than the contract specifies.
// Will return true if larger sequence_position is scheduled even in the absense of upgrade request,
// upgrade approved, or guar. upgrade keywords.
static int
kwUpgrade(DemandInfo *diPtr)
{
	if(diPtr->contract_seqpos < diPtr->seqpos)
		return(1);
	return(0);
}


// returns KW_False if false or, if true, returns the KeyWord value for whichever downgrade was requested
static KeyWord
kwDowngradeRequest(QLIST *q)
{
	QITEM *qi;
	int i, x;
	int count =  QGetCount(q);
	KW_Rec *kwRecPtr;

        for(i = 0, qi = QGetHead(q); i < count; ++i, qi = QGetNext(q, qi)) {
                kwRecPtr = (KW_Rec *) QGetObject(qi);
		for(x = 0; downGradeReqKW_List[x]; x++) { 
			if(kwRecPtr->kw == downGradeReqKW_List[x])
				return(kwRecPtr->kw);
		}
	}
	return(KW_False);
}

// returns KW_False if false or, if true, returns the KeyWord value for whichever upgrade was requested
static KeyWord
kwUpgradeRequest(QLIST *q)
{
	QITEM *qi;
	int i, x;
	int count =  QGetCount(q);
	KW_Rec *kwRecPtr;

        for(i = 0, qi = QGetHead(q); i < count; ++i, qi = QGetNext(q, qi)) {
                kwRecPtr = (KW_Rec *) QGetObject(qi);
		for(x = 0; upGradeReqKW_List[x]; x++) { 
			if(kwRecPtr->kw == upGradeReqKW_List[x])
				return(kwRecPtr->kw);
		}
	}
	return(KW_False);
}

// returns true if there are upgrade/downgrade req/approval/guaranteed keywords
static int
upgradeDowngradeKeywords(QLIST *q)
{
	if(kwDowngradeRequest(q) != KW_False)
		return(1);
	if(kwUpgradeRequest(q) != KW_False)
		return(1);
	if(kwExists(KW_Guaranteed_Upgrade, q))
		return(1);
	if(kwExists(KW_Guaranteed_Downgrade, q))
		return(1);
	if(kwExists(KW_Upgrade_Approved, q))
		return(1);
	if(kwExists(KW_Downgrade_Approved, q))
		return(1);
	return(0);
}

static int
upgradeKeywords(QLIST *q)
{
	if(kwUpgradeRequest(q) != KW_False)
		return(1);
	if(kwExists(KW_Guaranteed_Upgrade, q))
		return(1);
	if(kwExists(KW_Upgrade_Approved, q))
		return(1);
	return(0);
}

static int
downgradeKeywords(QLIST *q)
{
	if(kwDowngradeRequest(q) != KW_False)
		return(1);
	if(kwExists(KW_Guaranteed_Downgrade, q))
		return(1);
	if(kwExists(KW_Downgrade_Approved, q))
		return(1);
	return(0);
}

static int
ac_conv(Cnv_Type in, int in_value, Cnv_Type out)
{
	AC_Convert *acvPtr;

	acvPtr = &acv[0];
	while(acvPtr->seqpos) {
		if(in == SEQ_POS) {
			if(acvPtr->seqpos == in_value)
				break;
		}
		else if(in == AC_TYPE_ID) {
			if(acvPtr->actypeid == in_value)
				break;
		}
		else if(in == UPGRADE) {
			if(acvPtr->upGrade == in_value)
				break;
		}
		else if(in == DOWNGRADE) {
			if(acvPtr->downGrade == in_value)
				break;
		}
		else if(in == IDX_POS) {
			if(acvPtr->idxpos == in_value)
				break;
		}
		else if(in == AC_CD) {
			if(acvPtr->aircraftCd == in_value)
				break;
		}
		else
			return(0);
		++acvPtr;
	}
	if(acvPtr->seqpos == 0)
		return(0);
	
	if(out == SEQ_POS)
		return(acvPtr->seqpos);
	else if(out == AC_TYPE_ID)
		return(acvPtr->actypeid);
	else if(out == UPGRADE)
		return(acvPtr->upGrade);
	else if(out == DOWNGRADE)
		return(acvPtr->downGrade);
	else if(out == IDX_POS)
		return(acvPtr->idxpos);
	else if(out == AC_CD)
		return(acvPtr->aircraftCd);
	else
		return(0);
}


static double
getStandardRevenue(int contractid, int ac_type)
{
#ifdef JUST_TESTING
// Revenue to use for now
// CJ1      Bravo    CJ3       XLS      Sov
// 3000     3500     4000     5000     6000 --> old
// 4945     5445     5700     7145     8600 --> new
	switch(ac_type) {
	case 6: // CJ1
		return((double) 4945.00);
	case 5: // Bravo
		return((double) 5445.00);
	case 54: // CJ3
		return((double) 5700.00);
	case 11: // Excel
		return((double) 7145.00);
	case 52: // Sovereign
		return((double) 8600.00);
	default:
		return((double) 0.0);
	}
#endif // JUST_TESTING
	int idx_pos;
	switch(ac_type) {
	case 6: // CJ1
	case 5: // Bravo
	case 54: // CJ3
	case 11: // Excel
	case 52: // Sovereign
		idx_pos = ac_conv(AC_TYPE_ID, ac_type, IDX_POS);
		return((acTypeList + idx_pos)->standardRevenue);
	default:
		return((double) 0.0);
	}
}
// end incremental revenue functions


/*
Converts a null terminated string of hex digits to the byte value each 2 hex digits represents.
The left-most hex digit of the pair is placed in the high nibble of the byte.
Stops converting at the first character in the input string that isn't in the range of a-f, A-F or 0-9.
It also stops converting if (maxout - 1) number of output bytes have been placed in outbuf.
Returns arg outbuf.
outbuf is null terminated.
*/
static char *
hexToString(char *hex, char *outbuf, int maxout)
{
        char *p = hex;
        char *outPtr = outbuf;
        int c;
        int out;
        int count = 0;
        int outCount = 0;

        while(*p) {
                if(!strchr("abcdefABCDEF0123456789", (int) *p))
                        break;
                if(*p >= 'a' && *p <= 'f')
                        c = 'A' + (*p - 'a');
                else
                        c = *p;
                if(!(count % 2)) {
                        if(c >= 'A' && c <= 'F')
                                out = 10 + (c - 'A');
                        else
                                out = c - '0';
                }
                else {
                        if(c >= 'A' && c <= 'F')
                                out = out << 4 | (10 + (c - 'A'));
                        else if(c >= '0' && c <= '9')
                                out = out << 4 | (c - '0');
                        *outPtr++ = out;
                        ++outCount;
                        if(outCount >= (maxout -1))
                                break;
                }
                ++count;
                ++p;
        }
        *outPtr = '\0';
        return(outbuf);
}

static void
getPeakDayAdjustment(Demand *dPtr)
{
	DateTime dt_localtime;
	Date peak_day_date;
	AirportLatLon *allPtr;
	PeakDay *pdPtr, pdBuf;
	PeakDayContractRate *pdcrPtr, pdcrBuf;
	BINTREENODE *tmp, *tmp1;
	int isLDT; // 1= daylight saving time is in effect in local time, 0=not in effect

	allPtr = getAirportLatLonInfoByAptID(dPtr->outAirportID);
	if(allPtr) {
		dt_localtime = gmtToLocal(allPtr->tzid, dt_time_tToDateTime(dPtr->reqOut), &isLDT);
		peak_day_date = (Date) (dt_localtime >> 32);
		pdBuf.peak_day = peak_day_date;
		if((tmp = TreeSearch(peakDayRoot, &pdBuf, peakDayCompare))) {
			pdPtr = (PeakDay *) getTreeObject(tmp);
			if(pdPtr->level_id == 1){
					dPtr->earlyAdj = dPtr->lateAdj = optParam.peakDayLevel_1_Adj;
			}
			else if(dPtr->contractFlag == 1){
					dPtr->earlyAdj = dPtr->lateAdj = optParam.vectorWin;
			}
			else if(pdPtr->level_id == 2 || pdPtr->level_id == 3) {
				pdcrBuf.contractid = dPtr->contractID;
				pdcrBuf.level_id = pdPtr->level_id;
				if((tmp1 = TreeSearch(peakDayContractRateRoot, &pdcrBuf, peakDayContractRatesCompare))) {
					pdcrPtr = (PeakDayContractRate *) getTreeObject(tmp1);
					dPtr->earlyAdj = dPtr->lateAdj = (int) (pdcrPtr->flex_hours * 60.0);
				}
			}
		}
		else if(dPtr->contractFlag == 1){
				dPtr->earlyAdj = dPtr->lateAdj = optParam.vectorWin;
		}
	}
	else {
		logMsg(logFile,"%s Line %d: can't find airportid.\n", __FILE__,__LINE__);
	}

	//If the demand is within 8 hours of the fake runtime, no flex
	if(dPtr->reqOut < run_time_t + 60*60*8) // 11/24/08 ANG
		dPtr->earlyAdj = dPtr->lateAdj = 0;

	if (dPtr->recoveryFlag){ 		
		dPtr->earlyAdj = max(optParam.recoveryAdj_early, dPtr->earlyAdj); 
		dPtr->lateAdj = max(optParam.recoveryAdj_late, dPtr->lateAdj); 
	}

}

//Jintao's change

typedef enum
{ fieldindex_Org_AptID, fieldindex_ICAO, fieldindex_Lat, fieldindex_Long, fieldindex_commFlag, fieldindex_Dst_AptID, fieldindex_Minutes, fieldindex_Cab_cost, fieldindex_Ground_only
}MappingAptColumns;

typedef enum
{ col_ICAO, col_AptID, col_Lat, col_Lon, col_Tzid
}AirportLatLonColmns;

static int readAptList(MY_CONNECTION *myconn)
{  char *MappingListSQL="select twm.Org_AptID, twm.Org_ICAO, twm.Org_Latitude, twm.Org_Longitude, twm.Org_CommFlag, \n\
                      twm.Dest_AptID, twm.Travel_time, twm.Cab_cost, twm.Ground_only \n\
                      from  (   \n\
                      (select Org_AptID, Org_ICAO, Org_Latitude, Org_Longitude, Org_CommFlag, \n\
                       Dest_AptID, Dest_ICAO, Dest_Latitude, Dest_Longitude, Dest_CommFlag,  \n\
                       Travel_time, Cab_cost, Ground_only from mapping_airports where Travel_time!=0 order by Org_AptID) \n\
                      UNION  \n\
                      (select  Dest_AptID as Org_AptID, Dest_ICAO as Org_ICAO, Dest_Latitude as Org_Latitude, Dest_Longitude as Org_Longitude, Dest_CommFlag as Org_CommFlag,  \n\
                       Org_AptID as Dest_AptID, Org_ICAO as Dest_ICAO, Org_Latitude as Dest_Latitude, Org_Longitude as Dest_Longitude, Org_CommFlag as Dest_CommFlag,  \n\
                       Travel_time, Cab_cost, Ground_only from mapping_airports order by Org_AptID) \n\
                       order by Org_AptID, Travel_time)twm";
    //char *AirportLatLonSQL="select ICAO, AptId, Latitude, Longitude, Timezone from  AirportLatLon";
    MYSQL_RES *res;
	MYSQL_FIELD *cols;
	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	int oracle_aptNum=6000;
	int curr_orgAptID;
	AptMap *aptmapptr; //, *aptmapbuf;
	int numMaps;
	int curr_aptlistidx;
	int pre_aptlistidx;
	int difference;
	int i;//,k;
	
	if(!myDoQuery(myconn, MappingListSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	rowCount = mysql_num_rows(res);
	if(! rowCount) 
	{
		logMsg(logFile,"readAptList(): 0 rows returned.\n");
		return(0);
	}

    aptList[0].commFlag=2;
    aptList[0].aptMapping=NULL;
    aptList[0].numMaps=0;

    curr_orgAptID=0;
	pre_aptlistidx=0;
	numMaps=0;
    for(rows = 0; rows < rowCount; rows++)
	{ row = mysql_fetch_row(res);
		if(! row)
			break;
		if(! row[fieldindex_Org_AptID]) 
			continue;
		if (curr_orgAptID!=atoi(row[fieldindex_Org_AptID]))
		{   curr_aptlistidx=atoi(row[fieldindex_Org_AptID]);
		    difference=curr_aptlistidx-pre_aptlistidx;
			i=1;
			while(i<difference)
			{  //strcpy(aptList[pre_aptlistidx+i].ICAO, airportLatLonTabByAptId[pre_aptlistidx+i].icao);
			   //aptList[pre_aptlistidx+i].lat=airportLatLonTabByAptId[pre_aptlistidx+i].lat;
			   //aptList[pre_aptlistidx+i].lon=airportLatLonTabByAptId[pre_aptlistidx+i].lon;
		       if (strcmp(airportLatLonTabByAptId[pre_aptlistidx+i].icao, "NULL")==0)
			   { aptList[pre_aptlistidx+i].commFlag=9999;
                 aptList[pre_aptlistidx+i].aptMapping=NULL;
			     aptList[pre_aptlistidx+i].numMaps=0;
			     i++;
			   }
			   else
			   {
               aptList[pre_aptlistidx+i].commFlag=2;
               //aptList[pre_aptlistidx+i].tzid=airportLatLonTabByAptId[pre_aptlistidx+i].tzid;
               aptList[pre_aptlistidx+i].aptMapping=NULL;
			   aptList[pre_aptlistidx+i].numMaps=0;
			   i++;
			   }
			}
			if (numMaps>0)
			aptList[pre_aptlistidx].numMaps=numMaps;
            numMaps=0;
			//aptList[curr_aptlistidx].tzid=airportLatLonTabByAptId[curr_aptlistidx].tzid;
			//strcpy(aptList[curr_aptlistidx].ICAO, row[fieldindex_ICAO]);
			//aptList[curr_aptlistidx].lat=airportLatLonTabByAptId[curr_aptlistidx].lat;
			//aptList[curr_aptlistidx].lon=airportLatLonTabByAptId[curr_aptlistidx].lon;
            aptList[curr_aptlistidx].commFlag=atoi(row[fieldindex_commFlag]);
			aptList[curr_aptlistidx].aptMapping=(AptMap *) calloc((size_t) 1, sizeof(AptMap));
			aptmapptr=aptList[curr_aptlistidx].aptMapping;
			aptmapptr->airportID=atoi(row[fieldindex_Dst_AptID]);
			aptmapptr->cost=atof(row[fieldindex_Cab_cost]);
			aptmapptr->duration=atoi(row[fieldindex_Minutes]);
			aptmapptr->groundOnly=atoi(row[fieldindex_Ground_only]);
			curr_orgAptID=atoi(row[fieldindex_Org_AptID]);
			aptmapptr++;
			numMaps++;
			pre_aptlistidx=curr_aptlistidx;
			if (rows==(rowCount-1))
			{aptList[pre_aptlistidx].numMaps=numMaps;
			 break;}
			continue;
		}
		      if((aptList[curr_aptlistidx].aptMapping = (AptMap *)realloc((aptList[curr_aptlistidx].aptMapping),(numMaps+1)* sizeof(AptMap))) == NULL) 
			  {
				logMsg(logFile,"%s Line %d, Out of Memory in readAptList().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			   }
			   aptmapptr = &aptList[curr_aptlistidx].aptMapping[numMaps];
			   aptmapptr->airportID=atoi(row[fieldindex_Dst_AptID]);
			   aptmapptr->cost=atof(row[fieldindex_Cab_cost]);
			   aptmapptr->duration=atoi(row[fieldindex_Minutes]);
			   aptmapptr->groundOnly=atoi(row[fieldindex_Ground_only]);
			   aptmapptr++;
			   numMaps++;
			   if (rows==(rowCount-1))
			   {aptList[pre_aptlistidx].numMaps=numMaps;
			    break;
			   }
			   
			//else 
			 // continue;


	}
	while((curr_aptlistidx+1)<=(TOTAL_AIRPORTS_NUM))
	{ //strcpy(aptList[curr_aptlistidx+1].ICAO, airportLatLonTabByAptId[curr_aptlistidx+1].icao);
			   //aptList[curr_aptlistidx+1].lat=airportLatLonTabByAptId[curr_aptlistidx+1].lat;
			   //aptList[curr_aptlistidx+1].lon=airportLatLonTabByAptId[curr_aptlistidx+1].lon;
	           if (strcmp(airportLatLonTabByAptId[pre_aptlistidx+i].icao, "NULL")==0)
			   { aptList[curr_aptlistidx+1].commFlag=9999;
               //aptList[curr_aptlistidx+1].tzid=airportLatLonTabByAptId[curr_aptlistidx+1].tzid;
               aptList[curr_aptlistidx+1].aptMapping=NULL;
			   aptList[curr_aptlistidx+1].numMaps=0;
               curr_aptlistidx++;
			   }
			   else
			   {
               aptList[curr_aptlistidx+1].commFlag=2;
               //aptList[curr_aptlistidx+1].tzid=airportLatLonTabByAptId[curr_aptlistidx+1].tzid;
               aptList[curr_aptlistidx+1].aptMapping=NULL;
			   aptList[curr_aptlistidx+1].numMaps=0;
               curr_aptlistidx++;
			   }
	}
   

     return(0);
}
//Jintao's change-latest
typedef enum
{ field_index=0, field_OrgID, field_DestID, field_earlyDpt, field_lateArr, field_cost
}ODTableColmns;

typedef enum
{ field_odIndex=0, field_dptTm, field_arrTm, field_connAptID, field_unAvail
}OAGTableColmns;

typedef struct input_OagEntry
{
   int recId;
   int odIdx;
   time_t dptTm;
   time_t arrTm;
   int connAptID;
 }input_OagEntry;

static void readOagODTable(MY_CONNECTION *myconn)
{  char *ODSQL="select OD_index, Org_ID, Dest_ID, Early_Dpt, Late_Arr, Cost from ODTable";
   char *OAGSQL="select OD_index, Departure_time, Arrival_time, Conn_AptID, unAvailable from OAGTable";
   int index;
   int errNbr;
   DateTime DateTime_earlyDpt;
   DateTime DateTime_DptTm;
   DateTime DateTime_lateArr;
   DateTime DateTime_ArrTm;
   MYSQL_RES *res;
   MYSQL_FIELD *cols;
   MYSQL_ROW row;
   my_ulonglong rowCount, rows;
   MYSQL_RES *res_oag;
   MYSQL_FIELD *cols_oag;
   MYSQL_ROW row_oag;
   my_ulonglong rowCount_oag, rows_oag;
   int numOag;
   int curr_odidx,pre_odidx;
   int oag_odidx;
   OagEntry *popoagptr;
   
   

   if(!myDoQuery(myconn, ODSQL, &res, &cols)) 
   {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
   }
	rowCount = mysql_num_rows(res);
	if(! rowCount) 
	{
		logMsg(logFile,"readOagODTable(): 0 rows returned for ODTable.\n");
		return;
	}
    oDTable=(ODEntry*) calloc((size_t) rowCount, sizeof(ODEntry));
	numOD=rowCount;

	if (!oDTable)
	{  logMsg(logFile,"%s Line %d: Out of Memory in readOagODTable().",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	
	for(rows = 0; rows < rowCount; rows++)
	{ row = mysql_fetch_row(res);
		if(! row)
			break;
		if(! (index=atoi(row[field_index]))) 
			continue;
        oDTable[rows].commOrAptID=atoi(row[field_OrgID]);
		oDTable[rows].commDestAptID=atoi(row[field_DestID]);
		oDTable[rows].numOag=0;
		oDTable[rows].oagList=NULL;

		if((DateTime_earlyDpt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[field_earlyDpt], NULL, &errNbr)) == BadDateTime) 
	      { logMsg(logFile,"%s Line %d, Bad date in readOagODTable(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[field_earlyDpt]);
		    writeWarningData(myconn); exit(1);
	      }
        oDTable[rows].earlyDpt=DateTimeToTime_t(DateTime_earlyDpt);
        if((DateTime_lateArr = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[field_lateArr], NULL, &errNbr)) == BadDateTime) 
	      { logMsg(logFile,"%s Line %d, Bad date in readOagODTable(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row[field_lateArr]);
		    writeWarningData(myconn); exit(1);
	      }
		oDTable[rows].lateArr=DateTimeToTime_t(DateTime_lateArr);
		oDTable[rows].cost=atof(row[field_cost]);
	}
	mysql_free_result(res);

	if(!myDoQuery(myconn, OAGSQL, &res_oag, &cols_oag)) 
    { logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
	  writeWarningData(myconn); exit(1);
    }
	rowCount_oag = mysql_num_rows(res_oag);
	if(!rowCount_oag) 
	{
		logMsg(logFile,"%s Line %d:readOagODTable(): 0 rows returned for OAGTable.\n",__FILE__, __LINE__);
		return;
	}

	numOag=0;
	curr_odidx=0;
	for(rows_oag = 0; rows_oag < rowCount_oag; rows_oag++)
	{ row_oag = mysql_fetch_row(res_oag);
	   if(! row_oag)
	   break;
	   if(!atoi(row_oag[field_odIndex]))
	   continue;
	   oag_odidx=atoi(row_oag[field_odIndex]);
	   if(oag_odidx!=curr_odidx)
	   { pre_odidx=curr_odidx;
		 curr_odidx=oag_odidx;
	     
	     oDTable[curr_odidx-1].oagList=(OagEntry*)calloc((size_t)1, sizeof(OagEntry));
		 if(!oDTable[curr_odidx-1].oagList)
		 { logMsg(logFile,"%s Line %d: Out of Memory in readOagODTable().", __FILE__,__LINE__);
		   writeWarningData(myconn); exit(1);
		 }
		 popoagptr=oDTable[curr_odidx-1].oagList;
		 if(rows_oag!=0)
		 { oDTable[pre_odidx-1].numOag=numOag;
		   numOag=0;
		 }
	   }
	   else
	   { if((oDTable[curr_odidx-1].oagList = (OagEntry*)realloc((oDTable[curr_odidx-1].oagList),(numOag+1)* sizeof(OagEntry))) == NULL) 
			  {
				logMsg(logFile,"%s Line %d, Out of Memory in readOagODTable().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			   }
	     popoagptr=&oDTable[curr_odidx-1].oagList[numOag];
	   }  
		popoagptr->connAptID=atoi(row_oag[field_connAptID]);
           //popoagptr->unAvail=atoi(row_oag[field_unAvail]);
	    if((DateTime_DptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row_oag[field_dptTm], NULL, &errNbr)) == BadDateTime) 
	     { logMsg(logFile,"%s Line %d, Bad date in readOagODTable(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row_oag[field_dptTm]);
		    writeWarningData(myconn); exit(1);
	     }
        popoagptr->dptTm=DateTimeToTime_t(DateTime_DptTm);
        if((DateTime_ArrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row_oag[field_arrTm], NULL, &errNbr)) == BadDateTime) 
	    { logMsg(logFile,"%s Line %d, Bad date in readOagODTable(). errNbr=%d, date string=%s\n",__FILE__,__LINE__, errNbr, row_oag[field_arrTm]);
		  writeWarningData(myconn); exit(1);
	    }
        popoagptr->arrTm=DateTimeToTime_t(DateTime_ArrTm);
	    numOag++;
	}
	 
	  oDTable[curr_odidx-1].numOag=numOag;
	   
       mysql_free_result(res_oag);
		return;
}



/****************************************************************************************/
/*	Function   getAdjCrewAssg              Date last modified:   10/02/07 Jintao		*
*	Purpose:  get ontime1, ontime2, offtime1 and offtime2 for adjacent two crewassignments
    for a specific crew.									*
*/
/****************************************************************************************/
static void 
getAdjCrewAssg(PRE_Crew *preCrewPtr)
{ 
  int prevCrewDutyFlag=0;
  BINTREENODE *tmp, *tmp1;
  CrewData *cdptr, *cdptr1;
  DateTime ontime;
  DateTime offtime;
  DateTime prev_ontime;
  DateTime prev_offtime;
  DateTime succ_ontime;
  DateTime succ_offtime;
  char writetodbstring1[300];
  int making_on1_flag=0;
  int making_on2_flag=0;

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////
  pwStartDate = dt_time_tToDateTime(optParam.windowStart);
  pwEndDate = dt_time_tToDateTime(optParam.windowEnd);
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////


  if(preCrewPtr->bwCrewAssgRoot)
  {for(tmp=Minimum(preCrewPtr->bwCrewAssgRoot);tmp;tmp=Successor(tmp))
    { cdptr=(CrewData *) getTreeObject(tmp);
      ontime=cdptr->actual_starttime?cdptr->actual_starttime:cdptr->starttime;
      offtime=cdptr->actual_endtime?cdptr->actual_endtime:cdptr->endtime;
	  
      if(tmp1=Successor(tmp))
		 { cdptr1=(CrewData *)getTreeObject(tmp1);
		   succ_ontime=(cdptr1->actual_starttime?cdptr1->actual_starttime:cdptr1->starttime);
		   succ_offtime=(cdptr1->actual_endtime?cdptr1->actual_endtime:cdptr1->endtime);
		 }
	   else
		 { succ_ontime=0;
		   succ_offtime=0;
		 }
	  
	  if(ontime<pwStartDate && offtime<pwStartDate && succ_ontime<pwStartDate)
	   { prev_ontime=ontime;
	     prev_offtime=offtime;
		 prevCrewDutyFlag++;
		 continue;
	   }
	  else
	  {if(ontime>=pwStartDate && prevCrewDutyFlag==0)
		    { preCrewPtr->on1=0;
	          preCrewPtr->off1=0;
		      preCrewPtr->on2=ontime;
		      preCrewPtr->off2=offtime;
		      break;
	        }
		else
			{ preCrewPtr->on1=ontime;
				preCrewPtr->off1=offtime;
				preCrewPtr->on2=succ_ontime;
				preCrewPtr->off2=succ_offtime;
				break;
			}
	   }
	  // else
	  // {
		 //preCrewPtr->on1=ontime;
	  //   preCrewPtr->off1=offtime;
		 //preCrewPtr->on2=succ_ontime;
   //      preCrewPtr->off2=succ_offtime;
		 //break;
	  // }
	 // }
  }
  if(!tmp)
  { preCrewPtr->on2=0;
    preCrewPtr->off2=0;
	preCrewPtr->on1=ontime;
	preCrewPtr->off1=offtime;
  }
  }

  if (preCrewPtr->on1 && !preCrewPtr->on2) //RLZ: 03062008
  {
	  while ( AddDays(1, preCrewPtr->on1) <  pwStartDate )
	  { // Make on1 close to pwS within 1 day range 
		  preCrewPtr->on1 = AddDays(1, preCrewPtr->on1);
		  preCrewPtr->off1 = AddDays(1, preCrewPtr->off1);
		  fprintf(logFile,"Warning: Making crew assignment (on1) for crewID %5d. \n", preCrewPtr -> crewID);
		  making_on1_flag=1;
	  }
  }
  if(making_on1_flag==1)
  {       sprintf(writetodbstring1, "Warning: Making crew assignment (on1) for crewID %5d.", preCrewPtr->crewID);
	      if(errorNumber==0)
	        {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		       {logMsg(logFile,"%s Line %d, Out of Memory in getAdjCrewAssg().\n", __FILE__,__LINE__);
		        writeWarningData(myconn); exit(1);
			   }
			}
	      else
			{if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		       {logMsg(logFile,"%s Line %d, Out of Memory in getAdjCrewAssg().\n", __FILE__,__LINE__);
		        writeWarningData(myconn); exit(1);
	           }
			}
		   initializeWarningInfo(&errorinfoList[errorNumber]);
           errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
           strcpy(errorinfoList[errorNumber].group_name,"group_crew");
		   errorinfoList[errorNumber].crewid=preCrewPtr->crewID;
		   errorinfoList[errorNumber].format_number=9;
           strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
           errorNumber++;
		   making_on1_flag=0;
  }

  if (preCrewPtr->on2 && !preCrewPtr->on1)//RLZ: 03062008
 // if (preCrewPtr->on2 )
  {
	  while ( AddDays(-1, preCrewPtr->on2) > pwStartDate )
	  { // Make on2 close to pwS within 1 day range 
		  preCrewPtr->on2 = AddDays(-1, preCrewPtr->on2);
		  preCrewPtr->off2 = AddDays(-1, preCrewPtr->off2);
		  fprintf(logFile,"Warning: Making crew assignment (on2) for crewID %5d.\n", preCrewPtr->crewID);
		  making_on2_flag=1;
	  }
  }

//RLZ: need to revisit this and above, could on2 be on a day crew is not supposed to work? and for on1 too.

  if (preCrewPtr->on2 && preCrewPtr->on1 && dt_addToDateTime(Hours, 24, preCrewPtr->on1) < preCrewPtr->on2 )//Jintao: 12/29/2008
   {
	  while ( AddDays(-1, preCrewPtr->on2) > pwStartDate )
	  { // Make on2 close to pwS within 1 day range 
		  preCrewPtr->on2 = AddDays(-1, preCrewPtr->on2);
		  preCrewPtr->off2 = AddDays(-1, preCrewPtr->off2);
		  fprintf(logFile,"Warning: Updating crew assignment (on2) for crewID %5d.\n", preCrewPtr->crewID);
		  making_on2_flag=1;
	  }
  }
  



  if(making_on2_flag==1)
  {  sprintf(writetodbstring1, "Warning: Making crew assignment (on2) for crewID %5d.", preCrewPtr->crewID);	
	      if(errorNumber==0)
	        {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		       {logMsg(logFile,"%s Line %d, Out of Memory in getAdjCrewAssg().\n", __FILE__,__LINE__);
		        writeWarningData(myconn); exit(1);
			   }
			}
	      else
			{if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		       {logMsg(logFile,"%s Line %d, Out of Memory in getAdjCrewAssg().\n", __FILE__,__LINE__);
		        writeWarningData(myconn); exit(1);
	           }
		    }
		   initializeWarningInfo(&errorinfoList[errorNumber]);
           errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
           strcpy(errorinfoList[errorNumber].group_name,"group_crew");
		   errorinfoList[errorNumber].crewid=preCrewPtr->crewID;
		   errorinfoList[errorNumber].format_number=10;
           strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
           errorNumber++;
		   making_on2_flag=0;
	}


}									


/****************************************************************************************/
/*	Function   getCrewBlockTime              Date last modified:   10/05/07 Jintao 01/15/08 RLZ		*
*	Purpose:  get blocktime for the case where lastActivityLeg and pws are in the same crewassignment
    for a specific crew.									*
*/
/****************************************************************************************/
static int
getCrewBlockTime(PRE_Crew *preCrewPtr)
{   BINTREENODE *tmp;
    RawAircraftData *radPtr;
	int blocktime=0;
	int days, hours, minutes, seconds, msecs;
	int minutes_diff;
if(preCrewPtr->activityLegRoot)
  for(tmp=Minimum(preCrewPtr->activityLegRoot);tmp;tmp=Successor(tmp))
   {  radPtr=(RawAircraftData *) getTreeObject(tmp);
      if(radPtr->rec_intime > preCrewPtr->cscd_availDT && (strcmp(radPtr->rowtype,"logmgdleg") == 0 || strcmp(radPtr->rowtype,"mgdleg") == 0))
	     break;
      if(radPtr->rec_intime<=preCrewPtr->on1)
         continue;
      if(radPtr->rec_intime<=preCrewPtr->off1)
        //if(strcmp(radPtr->rowtype,"logmgdleg") == 0 || strcmp(radPtr->rowtype,"mgdleg") == 0 || strcmp(radPtr->rowtype,"ownersign") == 0)
		// RLZ: We do not need to consider ownersigning for block time.
		if(strcmp(radPtr->rowtype,"logmgdleg") == 0 || strcmp(radPtr->rowtype,"mgdleg") == 0)

         {dt_dateTimeDiff(radPtr->rec_intime, radPtr->rec_outtime, &days, &hours, &minutes, &seconds, &msecs);
          minutes_diff = (24 * 60 * days) + (60 * hours) + minutes;
	      blocktime+=minutes_diff;
         }
   }
  return(blocktime);
}

/****************************************************************************************/
/*	Function   getCrewPairStartEnd             Date last modified:   RLZ, 10/15/2007	*
*	Purpose:  get Crew Pair Start Time								*
*   Input: 2 crew ids and crewListRoot, where tour info is avaible.
*/
/****************************************************************************************/

static void getCrewPairStartEnd(CrewPair *cpPtr){ //  , CrewPairX *pxPtr0){
	Crew *cPtr, *c1Ptr, crewBuf;
	BINTREENODE *tmp3, *tmp4;
	DateTime tmpDate1, tmpDate2; 
	crewBuf.crewID = cpPtr->captainID; //pxPtr0->crewMemA_crewid;
	tmp3 = TreeSearch(crewListRoot, &crewBuf, crewListCrewidCmp);
	if(!tmp3) {
		logMsg(logFile,"%s Line %d, can't find crewid %d\n", __FILE__,__LINE__, crewBuf.crewID);
		writeWarningData(myconn); exit(1);
	}
	cPtr = (Crew *) getTreeObject(tmp3);

	crewBuf.crewID = cpPtr->flightOffID; // pxPtr0->crewMemB_crewid;
	tmp4 = TreeSearch(crewListRoot, &crewBuf, crewListCrewidCmp);
	if(!tmp4) {
		logMsg(logFile,"%s Line %d, can't find crewid %d\n", __FILE__,__LINE__, crewBuf.crewID);
		writeWarningData(myconn); exit(1);
	}
	c1Ptr = getTreeObject(tmp4);

	tmpDate1 = dt_addToDateTime(Minutes, - ((int)(24.0 * 60.0 * cPtr->startEarly)), dt_time_tToDateTime(cPtr->tourStartTm));
	tmpDate2 = dt_addToDateTime(Minutes, - ((int)(24.0 * 60.0 * c1Ptr->startEarly)), dt_time_tToDateTime(c1Ptr->tourStartTm));
	cpPtr->pairStartTm = DateTimeToTime_t(Max(tmpDate1, tmpDate2));


	tmpDate1 = dt_addToDateTime(Minutes, ((int)(24.0 * 60.0 * cPtr->stayLate)), dt_time_tToDateTime(cPtr->tourEndTm));
	tmpDate2 = dt_addToDateTime(Minutes, ((int)(24.0 * 60.0 * c1Ptr->stayLate)), dt_time_tToDateTime(c1Ptr->tourEndTm));
	cpPtr->pairEndTm = DateTimeToTime_t(Min(tmpDate1, tmpDate2));
}

void initializeWarningInfo(Warning_error_Entry *warningPtr)
{                                      warningPtr->local_scenarioid=0;
									   strcpy(warningPtr->group_name, "null");
									   warningPtr->aircraftid=0;
									   warningPtr->crewid=0;
									   strcpy(warningPtr->datatime_str,"null");
	                                   warningPtr->airportid=0;
									   warningPtr->demandid=0;
									   warningPtr->crewassgid=0;
									   warningPtr->crewpairid=0;
									   warningPtr->actypeid=0;
									   warningPtr->contractid=0;
	                                   warningPtr->minutes=0;
									   warningPtr->leg1id=0;
	                                   warningPtr->leg2id=0;
	                                   warningPtr->legindx=0;
									   warningPtr->acidx=0;
	                                   warningPtr->crewpairindx=0;
	                                   warningPtr->maintindx=0;
	                                   warningPtr->exclusionindex=0;
									   strcpy(warningPtr->filename,"null");
									   strcpy(warningPtr->line_number,"null");
	                                   warningPtr->format_number=0;
									   strcpy(warningPtr->notes,"null");
}
char *escapeQuotes(char *inp, char *out)
{

        char *src = inp;
        char *dst = out;


      while(*src) {
                *dst = *src;
                if(*src == '\'')
				{   dst++;
					*dst = *src;
				}
				if(*src=='\\')
					    *dst='/';
                        ++src;
						++dst;
	  }
        *dst = '\0';
        return(out);
}

/****************************************************************************************************
*	Function	addFakeRecords							Date last modified:  11/02/07 ANG
*	Purpose:	Add fake airport assignments to bring pilot home
*	Output :	Added fake airport assignments to demand list
****************************************************************************************************/

static int
addFakeRecords(PRE_Crew *preCrewPtr)
{
	MaintenanceRecord *mPtr;
	CrewPair *cpPtr;
	CrewEndTourRecord *fmPtr;
	int x, y;
	int *acPtr;
	char dbuf1[32];
	int addMe = 1;
	int currentAircraftID = 0;
	int currentCrewPairID = 0;

	BINTREENODE *tmp; // 06/13/08 ANG
	CrewData *crwPtr; 
	DateTime latestDt, endTime; // 06/13/08 ANG
	int days, hours, minutes, seconds, msecs; 

	extern BINTREENODE *crewidStarttimeEndtimeAcidRoot; // 06/13/08 ANG

	mPtr = (MaintenanceRecord *) calloc((size_t) 1, (size_t) sizeof(MaintenanceRecord));
	if(! mPtr) {
		logMsg(logFile,"%s Line %d, Out of Memory in addFakeRecords().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	cpPtr = (CrewPair *) calloc((size_t) 1, (size_t) sizeof(CrewPair));
	if(! cpPtr) {
		logMsg(logFile,"%s Line %d, Out of Memory to create CrewPair pointer in populateAcToFakeRecords().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	fmPtr = (CrewEndTourRecord *) calloc((size_t) 1, (size_t) sizeof(CrewEndTourRecord));
	if(! fmPtr) {
		logMsg(logFile,"%s Line %d, Out of Memory in addFakeRecords().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	//get aircraftID associated with preCrewPtr
	cpPtr = crewPairList;
	while(cpPtr->crewPairID){
		currentCrewPairID = cpPtr->crewPairID;
		//if(cpPtr->captainID == preCrewPtr->crewID){
		if(cpPtr->captainID == preCrewPtr->crewID || cpPtr->flightOffID == preCrewPtr->crewID){
			acPtr = cpPtr->aircraftID;
			if (acPtr){
				currentAircraftID = *acPtr;
			}
			break;
		} 
		//else if (cpPtr->flightOffID == preCrewPtr->crewID){
		//	acPtr = cpPtr->aircraftID;
		//	if (acPtr){
		//		currentAircraftID = *acPtr;
		//	}
		//	break;
		//} 
		else {
			cpPtr++;
		}
	}//end while

	if(currentAircraftID <= 0){
		return(0);
	}

	//START - First, determine the start and end times - 06/13/08 ANG
	latestDt = 0; //latestEnd = 0; 
      //for(tmp = Minimum(crewidStarttimeEndtimeAcidRoot); tmp; tmp = Successor(tmp)) {
      for(tmp = Minimum(preCrewPtr->bwCrewAssgRoot); tmp; tmp = Successor(tmp)) { // 06/20/08 ANG
		crwPtr = (CrewData *) getTreeObject(tmp);
		if(crwPtr->crewid != preCrewPtr->crewID)
			continue;
		if(crwPtr->starttime > dt_time_tToDateTime(optParam.windowEnd))
			continue;
		if(crwPtr->starttime > preCrewPtr->tourEndTm){
			if(crwPtr->starttime <= latestDt)
				continue;
			latestDt = crwPtr->starttime;
			continue;
		}
	}

	if(latestDt)
		dt_dateTimeDiff(preCrewPtr->tourEndTm, latestDt, &days, &hours, &minutes, &seconds, &msecs);
	else
		days = -1;

	endTime = dt_addToDateTime(Hours, (days+1)*24, preCrewPtr->tourEndTm);
	//END - 06/13/08 ANG
	//	&& mPtr->airportID == getBaseToCsiMapping(preCrewPtr->endLoc)
	for(x = 0, mPtr = maintList; x < numMaintenanceRecord; ++x, ++mPtr) {
		if(mPtr->aircraftID && mPtr->airportID){
			if( addMe == 1  && mPtr->aircraftID == currentAircraftID							
							&& (mPtr->startTm <= DateTimeToTime_t(dt_addToDateTime(Minutes, 4*60, endTime))
							&&  mPtr->endTm >= DateTimeToTime_t(dt_addToDateTime(Minutes, -6*60, endTime)))){
							//&& (mPtr->startTm <= DateTimeToTime_t(dt_addToDateTime(Minutes, 2*optParam.turnTime, preCrewPtr->tourEndTm))
							//||  mPtr->endTm >= DateTimeToTime_t(dt_addToDateTime(Minutes, -2*optParam.turnTime, preCrewPtr->tourEndTm)))){
				addMe = 0;
				//if( x >= numMaintenanceRecord - numFakeMaintenanceRecord &&
				if( mPtr->aircraftID == currentAircraftID // 11/12/07 ANG
					&& mPtr->airportID == getBaseToCsiMapping(preCrewPtr->endLoc)
					&& mPtr->startTm == DateTimeToTime_t(dt_addToDateTime(Minutes, 0, endTime))
					&& mPtr->endTm == DateTimeToTime_t(dt_addToDateTime(Minutes, 1, endTime))){
					//&& mPtr->startTm == DateTimeToTime_t(dt_addToDateTime(Minutes, 0, preCrewPtr->tourEndTm))
					//&& mPtr->endTm == DateTimeToTime_t(dt_addToDateTime(Minutes, 1, preCrewPtr->tourEndTm))){
					//update second crew information for the corresponding CrewEndTourRecord
					for(y =	0, fmPtr = crewEndTourList; y < crewEndTourCount; ++y){
						if( fmPtr->aircraftID == currentAircraftID 
							&& fmPtr->airportID == mPtr->airportID
							&& fmPtr->startTm == mPtr->startTm
							&& fmPtr->endTm == mPtr->endTm
							&& fmPtr->recordType == 1){
							fmPtr->crewID2 = preCrewPtr->crewID;
							fmPtr->recordType = 2;
							break;
						}
						++fmPtr;
					}//end for
				}//end if
				else { // 11/12/07 ANG
					//fake record is NOT added to maintList because it is overlapping with a true airport assignment at pilot's base
					//however, it is still added to crewEndTourList
					for(y = 0, fmPtr = crewEndTourList; y < crewEndTourCount; ++y){
						++fmPtr;
					}
					fmPtr->recordID = mPtr->maintenanceRecordID ? mPtr->maintenanceRecordID : 0;
					fmPtr->aircraftID = currentAircraftID;
					fmPtr->airportID = getBaseToCsiMapping(preCrewPtr->endLoc);
					fmPtr->crewID1 = preCrewPtr->crewID;
					fmPtr->recordType = 0;
					fmPtr->covered = -1;
					fmPtr->wrongCrew = -1;
					//fmPtr->cost = 0.0;
					crewEndTourCount++;
				}//end else
			}//end if 
			
			if ( addMe == 1 && numFakeMaintenanceRecord >= optParam.maxFakeMaintRec){ // 11/12/07 ANG
				//fake record is NOT added to maintList because maxFakeMaintRec is reached
				//however, it is still added to crewEndTourList
				addMe = 0;
				for(y = 0, fmPtr = crewEndTourList; y < crewEndTourCount; ++y){
					++fmPtr;
				}
				fmPtr->recordID = 0;
				fmPtr->aircraftID = currentAircraftID;
				fmPtr->airportID = getBaseToCsiMapping(preCrewPtr->endLoc);
				fmPtr->crewID1 = preCrewPtr->crewID;
				fmPtr->recordType = 3;
				fmPtr->covered = -1;
				fmPtr->assignedDemandID = -1;
				fmPtr->assignedDemandInd = -1;
				fmPtr->wrongCrew = -1;
				//fmPtr->cost = 0.0;
				crewEndTourCount++;			
			}//end if

			//START - If this is a SIC, do not create a fake record - 06/19/08 ANG
			if ( addMe == 1 && preCrewPtr->position == 2){ 
				addMe = 0;
				//however, it is still added to crewEndTourList
				for(y = 0, fmPtr = crewEndTourList; y < crewEndTourCount; ++y){
					++fmPtr;
				}
				fmPtr->recordID = 0;
				fmPtr->aircraftID = currentAircraftID;
				fmPtr->airportID = getBaseToCsiMapping(preCrewPtr->endLoc);
				fmPtr->crewID1 = preCrewPtr->crewID;
				fmPtr->recordType = 3;
				fmPtr->covered = -1;
				fmPtr->assignedDemandID = -1;
				fmPtr->assignedDemandInd = -1;
				fmPtr->wrongCrew = -1;
				//fmPtr->cost = 0.0;
				crewEndTourCount++;			
			}//end if
			//END - If this is a SIC, do not create a fake record - 06/19/08 ANG

		}//end if
	}//end for

	if(addMe == 1){
		mPtr->maintenanceRecordID = 1000000 + preCrewPtr->crewID;
		mPtr->aircraftID = currentAircraftID;
		mPtr->airportID = getBaseToCsiMapping(preCrewPtr->endLoc);
		//mPtr->startTm = DateTimeToTime_t(dt_addToDateTime(Minutes, 0, preCrewPtr->tourEndTm));
		//mPtr->endTm = DateTimeToTime_t(dt_addToDateTime(Minutes, 1, preCrewPtr->tourEndTm));
		mPtr->startTm = DateTimeToTime_t(dt_addToDateTime(Minutes, 0, endTime));
		mPtr->endTm = DateTimeToTime_t(dt_addToDateTime(Minutes, 1, endTime));
		mPtr->apptType = 3; 

		numMaintenanceRecord++;
		numFakeMaintenanceRecord++;

		if(verbose){
			//fprintf(logFile, "Added fake record for crewID = %d with tourEndTm = %s \n", preCrewPtr->crewID, dt_DateTimeToDateTimeString(preCrewPtr->tourEndTm, dbuf1, "%Y/%m/%d %H:%M"));
			fprintf(logFile, "Added fake record for crewID = %d with tourEndTm = %s \n", preCrewPtr->crewID, dt_DateTimeToDateTimeString(endTime, dbuf1, "%Y/%m/%d %H:%M"));
		}

		for(x = 0, fmPtr = crewEndTourList; x < crewEndTourCount; ++x){
			++fmPtr;
		}
		fmPtr->recordID = 1000000 + preCrewPtr->crewID;
		fmPtr->aircraftID = currentAircraftID;
		//fmPtr->airportID = preCrewPtr->startLoc;
		fmPtr->airportID = getBaseToCsiMapping(preCrewPtr->endLoc); 
		//fmPtr->startTm = DateTimeToTime_t(dt_addToDateTime(Minutes, 0, preCrewPtr->tourEndTm));
		//fmPtr->endTm = DateTimeToTime_t(dt_addToDateTime(Minutes, 1, preCrewPtr->tourEndTm));
		fmPtr->startTm = DateTimeToTime_t(dt_addToDateTime(Minutes, 0, endTime));
		fmPtr->endTm = DateTimeToTime_t(dt_addToDateTime(Minutes, 1, endTime));
		fmPtr->crewID1 = preCrewPtr->crewID;
		fmPtr->recordType = 1;
		fmPtr->covered = -1;
		fmPtr->assignedDemandID = -1;
		fmPtr->assignedDemandInd = -1;
		//fmPtr->cost = 0.0;
		crewEndTourCount++;

	}//end if(addMe == 1)

	return(0);
}//end of function



/****************************************************************************************************
*	Function	printFakeRecords							Date last modified:  11/02/07 ANG
*	Purpose:	Print fake records added to bring pilot home
****************************************************************************************************/
static void
printFakeRecords(void)
{
	if(verbose) {
		MaintenanceRecord *tPtr;
//		CrewEndTourRecord *fmPtr; // 11/12/07 ANG
		int x;
		int errNbr1;
		int errNbr2;
		char tbuf1[32];
		char tbuf2[32];

		tPtr = (MaintenanceRecord *) calloc((size_t) 1, (size_t) sizeof(MaintenanceRecord));
		if(! tPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory while printing maintList in printFakeRecords().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		logMsg(logFile,"maintList after adding fake records:\n");
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+\n");
		fprintf(logFile,"| maintRecordID | aircraftID | airportID |          startTm |            endTm | apptType |\n");
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+\n");
		for(x = 0, tPtr = maintList; x < numMaintenanceRecord; ++x, ++tPtr) {
			fprintf(logFile,"| %13d | %10d | %9d | %16s | %16s | %8d |\n",
				tPtr->maintenanceRecordID,
				tPtr->aircraftID,
				tPtr->airportID,
				(tPtr->startTm) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->startTm))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				(tPtr->endTm) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->endTm))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				tPtr->apptType);
		}
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+\n\n\n\n");
		fflush(logFile);
	//printCrewEndTourList();
	}//end if(verbose)
}//end of function

/****************************************************************************************************
*	Function	printCrewEndTourList							Date last modified:  11/14/07 ANG
*	Purpose:	Print crewEndTourList records 
****************************************************************************************************/
void
printCrewEndTourList(void)
{
	if(verbose && (optParam.autoFlyHome == 1)) {
		CrewEndTourRecord *fmPtr;
		int x;
		int errNbr1;
		int errNbr2;
		char tbuf1[32];
		char tbuf2[32];

		fmPtr = (CrewEndTourRecord *) calloc((size_t) 1, (size_t) sizeof(CrewEndTourRecord));
		if(! fmPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory while printing demandList in printFakeRecords().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		logMsg(logFile,"crewEndTourList so far:\n");
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+---------+---------+----------+---------------+---------------+-----------+-----------+------------+\n");
		fprintf(logFile,"|     recordID  | aircraftID | airportID |          startTm |            endTm |  recType | crewID1 | crewID2 |crewPairID| assignedDmdID | assignedDmdInd| flownHome?| wrongCrew?|flyHome cost|\n");
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+---------+---------+----------+---------------+---------------+-----------+-----------+------------+\n");
		for(x = 0, fmPtr = crewEndTourList; x < crewEndTourCount; ++x, ++fmPtr) {
			fprintf(logFile,"| %13d | %10d | %9d | %16s | %16s | %8d | %7d | %7d | %8d | %13d | %13d | %9d | %9d | %10.2f |\n",
				fmPtr->recordID,
				fmPtr->aircraftID,
				fmPtr->airportID,
				(fmPtr->startTm) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(fmPtr->startTm))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				(fmPtr->endTm) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(fmPtr->endTm))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				fmPtr->recordType,
				fmPtr->crewID1,
				(fmPtr->crewID2) ? fmPtr->crewID2 : 0,
				(fmPtr->crewPairID) ? fmPtr->crewPairID : 0,
				fmPtr->assignedDemandID,
				fmPtr->assignedDemandInd,
				fmPtr->covered,
				fmPtr->wrongCrew);
				//fmPtr->wrongCrew,
				//fmPtr->cost);
		}
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+---------+--------------------+---------------+---------------+-----------+-----------+------------+\n\n\n\n");
		fflush(logFile);
	}//end if
}//end function

/****************************************************************************************************
*	Function	getBaseToCsiMapping								Date last modified:  11/15/07 ANG
*	Purpose:	Given pilot base's airportID, return CSI's airportID
*				This function is used to fly pilots to his CSI (instead of to his base)
				This function can be modified later when Base to CSI mapping is stored in a table
****************************************************************************************************/
static int
getBaseToCsiMapping(int baseAirportID)
{
	int i;

	i = baseAirportID;
	switch (baseAirportID)
	{
	case 538: i = 538; break;
	case 696: i = 694; break;
	case 766: i = 766; break;
	case 773: i = 773; break;
	case 850: i = 840; break;
	case 936: i = 936; break;
	case 989: i = 989; break;
	case 1018: i = 1018; break;
	case 1030: i = 1030; break;
	case 1170: i = 1110; break;
	case 1455: i = 1454; break;
	case 1607: i = 4959; break;
	case 1827: i = 3487; break;
	case 1973: i = 1978; break;
	case 2008: i = 936; break;
	case 2529: i = 1814; break;
	case 2533: i = 2533; break;
	case 2886: i = 2886; break;
	case 3160: i = 3453; break;
	case 3281: i = 3281; break;
	case 3453: i = 3453; break;
	case 3492: i = 3487; break;
	case 4244: i = 4244; break;
	case 4363: i = 4362; break;
	case 4412: i = 4362; break;
	case 4577: i = 4577; break;
	case 4692: i = 840; break;
	}

	if(i > 0) 
		return i;
	else {
		fprintf(logFile, "Could not find CSI airportID for Base airportID = %d.\n", baseAirportID);
		writeWarningData(myconn); exit(1);
	}
}//end of function

/****************************************************************************************************
*	Function	printCrewEndTourSummary							Date last modified:  11/21/07 ANG
*	Purpose:	Print crewEndTourList summary 
****************************************************************************************************/
void
printCrewEndTourSummary(void)
{
	if(verbose && (optParam.autoFlyHome == 1)) {
		CrewEndTourRecord *fmPtr;
		int x;
//		int errNbr1;
//		int errNbr2;
//		char tbuf1[32];
//		char tbuf2[32];
		int crewSentHomeCount = 0;

		fmPtr = (CrewEndTourRecord *) calloc((size_t) 1, (size_t) sizeof(CrewEndTourRecord));
		if(! fmPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory while printing demandList in printCrewEndTourSummary().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		fprintf(logFile,"Crews Flown Home:\n");
		//fprintf(logFile,"+---------+---------+------------+---------------+----------+-----------------------------------------------------------------+\n");
		//fprintf(logFile,"| crewID1 | crewID2 | aircraftID | CSI airportID |   Cost   |   Notes                                                         |\n");
		//fprintf(logFile,"+---------+---------+------------+---------------+----------+-----------------------------------------------------------------+\n");
		fprintf(logFile,"+---------+---------+------------+---------------+\n");
		fprintf(logFile,"| crewID1 | crewID2 | aircraftID | CSI airportID |\n");
		fprintf(logFile,"+---------+---------+------------+---------------+\n");
		for(x = 0, fmPtr = crewEndTourList; x < crewEndTourCount; ++x, ++fmPtr) {
			if (fmPtr->covered == 1 && fmPtr->wrongCrew == 0){
				//fprintf(logFile,"| %7d | %7d | %10d | %13d | %8.2f | %63s |\n",
				fprintf(logFile,"| %7d | %7d | %10d | %13d |\n",
				fmPtr->crewID1,
				(fmPtr->crewID2) ? fmPtr->crewID2 : 0,
				fmPtr->aircraftID,
				fmPtr->airportID);
				//fmPtr->cost,
				//(fmPtr->recordType == 0) ? "Flown home since the aircraft has a scheduled maint/appointment" : "");
				if(fmPtr->crewID2) crewSentHomeCount = crewSentHomeCount + 2;
				else crewSentHomeCount++;
			}
		}
		fprintf(logFile,"+---------+---------+------------+---------------+\n\n");
		fflush(logFile);

		fprintf(logFile,"Crews NOT Flown Home:\n");
		//fprintf(logFile,"+---------+---------+------------+---------------+-------------------------------------------------------------------+\n");
		//fprintf(logFile,"| crewID1 | crewID2 | aircraftID | CSI airportID |   Notes                                                           |\n");
		//fprintf(logFile,"+---------+---------+------------+---------------+-------------------------------------------------------------------+\n");
		fprintf(logFile,"+---------+---------+------------+---------------+\n");
		fprintf(logFile,"| crewID1 | crewID2 | aircraftID | CSI airportID |\n");
		fprintf(logFile,"+---------+---------+------------+---------------+\n");
		for(x = 0, fmPtr = crewEndTourList; x < crewEndTourCount; ++x, ++fmPtr) {
			if (fmPtr->covered <= 0){
				//fprintf(logFile,"| %7d | %7d | %10d | %13d | %65s |\n",
				fprintf(logFile,"| %7d | %7d | %10d | %13d |\n",
				fmPtr->crewID1,
				(fmPtr->crewID2) ? fmPtr->crewID2 : 0,
				fmPtr->aircraftID,
				fmPtr->airportID);
				//(fmPtr->recordType == 0) ? "Not considered due to conflict with a scheduled maint/appointment" : "");
			}
			else if (fmPtr->covered == 1 && fmPtr->wrongCrew == 1){
				//fprintf(logFile,"| %7d | %7d | %10d | %13d | %65s |\n",
				fprintf(logFile,"| %7d | %7d | %10d | %13d | %s \n",
				fmPtr->crewID1,
				(fmPtr->crewID2) ? fmPtr->crewID2 : 0,
				fmPtr->aircraftID,
				fmPtr->airportID,
				"WARNING: Fake airport appointment is performed by different crews");
			}
		}
		fprintf(logFile,"+---------+---------+------------+---------------+\n\n");
		fflush(logFile);

		fprintf (logFile, "Number of crews sent home = %3d (%2.1f %% of all crews going off-tour).\n\n", 
		crewSentHomeCount, (100 * (double) crewSentHomeCount/ (double) crewEndTourCount));

	}//end if
}//end function


/****************************************************************************************************
*	Function	printAcList							Date last modified:  02/28/08 ANG
*	Purpose:	Print aircraft list 
****************************************************************************************************/
void
printAcList(void)
{
	if(verbose) {
		Aircraft *tPtr;
		int x;
		int errNbr;
		char tbuf[32];//, tbuf1[32];

		tPtr = (Aircraft *) calloc((size_t) 1, (size_t) sizeof(Aircraft));
		if(! tPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory while printing acList.\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		logMsg(logFile,"Final acList:\n");
		//fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+----------+\n");
		//fprintf(logFile,"| aircraftID | aircraftTypeID | sequencePosn | availAirportID | ICAO  | availFboID | availDt          | maintFlag | intlcert |\n");
		//fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+----------+\n");
		fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+----------+\n"); // 02/28/08 ANG
		fprintf(logFile,"| aircraftID | aircraftTypeID | sequencePosn | availAirportID | ICAO  | availFboID | availDt          | maintFlag | intlcert |\n"); // 02/28/08 ANG
		fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+----------+\n"); // 02/28/08 ANG
		for(x=0, tPtr = acList; x < aircraftCount; ++x, ++tPtr) {
			fprintf(logFile,"| %10d | %14d | %12d | %14d | %5s | %10d | %16s | %9d | %8d |\n",
				tPtr->aircraftID,
				tPtr->aircraftTypeID,
				tPtr->sequencePosn,
				tPtr->availAirportID,
				tPtr->availAptICAO,
				tPtr->availFboID,
				(tPtr->availDT) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				//(tPtr->nextLegStartTm) ?
		  //  		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
				//	asctime(gmtime(&(tPtr->nextLegStartTm))), NULL, &errNbr),tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				tPtr->maintFlag,
				tPtr->intlCert);
		}
		//fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+----------+\n\n");
		fprintf(logFile,"+------------+----------------+--------------+----------------+-------+------------+------------------+-----------+----------+\n"); // 02/28/08 ANG
		fflush(logFile);
	}
}//end function


/****************************************************************************************************
*	Function	initAcSchedLegIndList							Date last modified:  03/02/08 ANG
*	Purpose:	Initialize schedLegIndList schedCrPrIndList for all aircraft 
****************************************************************************************************/
void
initAcSchedLegIndList(void)
{
	int x, y;
	Aircraft *tPtr;

	tPtr = (Aircraft *) calloc((size_t) 1, (size_t) sizeof(Aircraft));
	if(! tPtr) {
		logMsg(logFile,"%s Line %d, Out of Memory in initAcSchedLegIndList().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	for(x=0, tPtr = acList; x < aircraftCount; ++x, ++tPtr) {
		for(y=0; y < MAX_LEGS; y++){
			tPtr->schedLegIndList[y] = -1;
			tPtr->schedCrPrIndList[y] = -1;
		}
		tPtr->countCrPrToday = 0;
	}

	fprintf(logFile, "schedLegIndList, schedCrPrIndList, and countCrPrToday for Aircraft are initialized,\n\n");
}//end function


/****************************************************************************************************
*	Function	printMaintList							Date last modified:  04/22/08 ANG
*	Purpose:	Print printMaintList records 
****************************************************************************************************/
void
printMaintList(void)
{
		Demand *tPtr;
		int x;
		int errNbr1;
		int errNbr2;
		char tbuf1[32];
		char tbuf2[32];
		extern numOptDemand;

		tPtr = (Demand *) calloc((size_t) 1, (size_t) sizeof(Demand));
		if(! tPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory while printing demandList in printMaintList().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		logMsg(logFile,"maintList from demandList:\n");
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+\n");
		fprintf(logFile,"| maintRecordID | aircraftID | airportID |          startTm |            endTm | apptType | index\n");
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+\n");
		for(x = 0, tPtr = demandList; x < numOptDemand; ++x, ++tPtr) {
			if (tPtr->isAppoint > 0){
				fprintf(logFile,"| %13d | %10d | %9d | %16s | %16s | %8d | %d\n",
				tPtr->demandID,
				tPtr->aircraftID,
				tPtr->outAirportID,
				(tPtr->reqOut) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->reqOut))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				(tPtr->reqIn) ?
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(tPtr->reqIn))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M") : "0000/00/00 00:00",
				tPtr->isAppoint, x);
			}
		}
		fprintf(logFile,"+---------------+------------+-----------+------------------+------------------+----------+\n\n");
		fflush(logFile);

}//end function


typedef enum {
	dmdnumCJ1,dmdnumBRV, dmdnumCJ3, dmdnumEXL, dmdnumSOV=4
} dmdnumbyzoneSqlColumns;

/****************************************************************************************************
*	Function	readContingencyFkDemand							            Date last modified:  03/11/08 Jintao
*	Purpose:	read contingecy fake demand 
****************************************************************************************************/
static int readContingencyFkDemand(MY_CONNECTION *myconn)
{   
	extern char *demandnumbyzoneSQL;

//	char sqlbuf[2000];
    char *demandnumbyzoneSQL_query;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
//	int idx;
//	Demand *tPtr;
//	DateTime dt;
//	char *querystrbyarea;
	int demandnumbyzone[MAX_WINDOW_DURATION][MAX_DAY_DIVISION][ZONE_NUM][ACTYPE_NUM];
	int day,day_division,zoneid;
	char *variables[128];
	char timestartstr[64];
	char timeendstr[64];
	char zoneidstr[16];
    DateTime timestart_day, timeend_day, pwstart, pwend, timestart, timeend, tmp_value, tmp_timeend_day;
	char tbuf1[32];
    char tbuf2[32];
//    char tbuf3[32];
	char *paramVal, *s1;
	int vc, x, sc;
	char *paramval[5]={"","","","",""};
	int totalday;
	int type;
	//double contingency_prob[ACTYPE_NUM] = {1, 2, 3, 4, 5};
	int actypeidx;
    int maxfkdmdnum;
	int fkdmdduration;
	int totalfkdmdadded;
 
    pwstart = dt_time_tToDateTime(optParam.windowStart);
	pwend = dt_time_tToDateTime(optParam.windowEnd);
	//startFkDmdIdx = numDemandAllocd;
     
	//contingency_prob[ACTYPE_NUM] = {0.045, 0.12, 0.10, 0.15, 0.17};
	vc = getVars(demandnumbyzoneSQL, 128, variables);
	totalday = 1;
	totalfkdmdadded = 0;
    tmp_timeend_day = dt_addToDateTime(Hours, -10, pwend);
    //  We might modify later
	maxfkdmdnum = 1;
    fkdmdduration = 4;
	//

	readHubsbyZones();  //get aptid of hubs in zones
	while(pwstart < tmp_timeend_day)
	{  totalday = totalday +1;
	   tmp_timeend_day = dt_addToDateTime(Hours, -24,  tmp_timeend_day);
	}

    for(day = 0; day < MAX_WINDOW_DURATION; day++)
	  for(day_division = 0; day_division < MAX_DAY_DIVISION; day_division++)
         for(zoneid = 1; zoneid <= ZONE_NUM; zoneid++)
			for(type = 0; type < ACTYPE_NUM; type++)
                  demandnumbyzone[day][day_division][zoneid-1][type] = -1;

	//tmp_timeend_day = pwend;
	//strcpy(sqlbuf, demandnumbyzoneSQL);
	logMsg(logFile, "Jintao's test on demandnumquery\n");
    fprintf(logFile, "##############################################\n");
	//timeend_day = dt_addToDateTime(Hours, -10,  pwend);
	timeend_day = pwend;
    for(day = 0; day < totalday; day++) 
	{	 if(day == 0)
	     { tmp_value = dt_addToDateTime(Hours, -4, timeend_day);
	     }
	     else
	     { //tmp_value = dt_addToDateTime(Hours, -18, timeend_day);
			 tmp_value = dt_addToDateTime(Hours, -12, timeend_day);
	     }
	     timestart_day = tmp_value > pwstart ? tmp_value : pwstart;
         timeend = timeend_day;
		 for(day_division = 0; day_division < MAX_DAY_DIVISION; day_division++)
		 {  if(timeend <= pwstart)
				 break;
		     if(day == 0)
			   {  timestart = timestart_day;
			      day_division = MAX_DAY_DIVISION -1;
				  //ignore day 0
				  break;
				  //
			   }
			  else 
				 //timestart = dt_addToDateTime(Hours, -18/MAX_DAY_DIVISION, timeend);
				   timestart = dt_addToDateTime(Hours, -12/MAX_DAY_DIVISION, timeend);
			 if(timestart < pwstart)
				 timestart = pwstart;
		     strcpy(timestartstr, dt_DateTimeToDateTimeString(timestart, tbuf1, "%Y/%m/%d %H:%M"));                        
			 strcpy(timeendstr, dt_DateTimeToDateTimeString(timeend, tbuf2, "%Y/%m/%d %H:%M"));
             *(paramval + 0) = strdup(timestartstr);
             *(paramval + 1) = strdup(timeendstr);
			 logMsg(logFile, "####From %s to %s\n", timestartstr, timeendstr);
			 for(zoneid = 1; zoneid <= ZONE_NUM; zoneid++)
			 { sprintf(zoneidstr,"%d", zoneid);
			   *(paramval + 2) = strdup(zoneidstr);
			   demandnumbyzoneSQL_query = strdup(demandnumbyzoneSQL);
			   for(x = 0; x < vc; ++x)
			   { paramVal = strdup(*(paramval + x));
			      s1 = substitute(demandnumbyzoneSQL_query, makevar(*(variables + x)), paramVal, &sc);
				  if(!s1) 
				  { logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			        exit(1);
		          }
		          demandnumbyzoneSQL_query = strdup(s1);
		          if(!demandnumbyzoneSQL_query ) 
				  {
			       logMsg(logFile,"Out of memory.\n");
			       exit(1);
				  }
			   }
			   //logMsg(logFile, "%s\n", demandnumbyzoneSQL_query);
               if(!myDoQuery(myconn, demandnumbyzoneSQL_query, &res, &cols)) 
			   {
		         logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		         writeWarningData(myconn); exit(1);
	           }
	           rowCount = mysql_num_rows(res);
	           if(! rowCount) 
			   {
		        logMsg(logFile,"readContingencyFkDemand(): 0 rows returned.\n");
		        return(0);
	           }
			   for(rows = 0; rows < rowCount; rows++)
			   { row = mysql_fetch_row(res);
		         if(! row)
			      break;
                 demandnumbyzone[day][day_division][zoneid-1][dmdnumCJ1] = atoi(row[dmdnumCJ1]);
                 demandnumbyzone[day][day_division][zoneid-1][dmdnumBRV] = atoi(row[dmdnumBRV]);
                 demandnumbyzone[day][day_division][zoneid-1][dmdnumCJ3] = atoi(row[dmdnumCJ3]);
				 demandnumbyzone[day][day_division][zoneid-1][dmdnumEXL] = atoi(row[dmdnumEXL]);
				 demandnumbyzone[day][day_division][zoneid-1][dmdnumSOV] = atoi(row[dmdnumSOV]);
			   }
			   logMsg(logFile,"Zone %d: %d %d %d %d %d\n",
				   zoneid,
				   demandnumbyzone[day][day_division][zoneid-1][dmdnumCJ1],
				   demandnumbyzone[day][day_division][zoneid-1][dmdnumBRV],
                   demandnumbyzone[day][day_division][zoneid-1][dmdnumCJ3],
				   demandnumbyzone[day][day_division][zoneid-1][dmdnumEXL],
				   demandnumbyzone[day][day_division][zoneid-1][dmdnumSOV]); 
			   for(actypeidx = 0 ; actypeidx < 5; actypeidx++)
			   {      if(demandnumbyzone[day][day_division][zoneid-1][actypeidx] > 0)
					  addFkDemand(demandnumbyzone[day][day_division][zoneid-1][actypeidx], maxfkdmdnum, day_division, timestart, timeend, actypeidx, zoneid, fkdmdduration, &totalfkdmdadded);
			   }
			 }
		    logMsg(logFile, "##############################################\n");
		    timeend = timestart;
		 }
		 if(timeend_day <= pwstart)
			break;
		 if(day ==0)
			 //timeend_day = dt_addToDateTime(Hours, -10,  timeend_day);
		 { timeend_day = dt_addToDateTime(Hours, -11,  timeend_day);
		   //ignore day 0
		   continue;
		 }
		 else
		 {
		 timestart_day = dt_addToDateTime(Hours, -24, timestart_day); 
		 timeend_day = dt_addToDateTime(Hours, -24, timeend_day);
		 }
	}
	/*
	for(day = 0; day < optParam.planningWindowDuration; day++) 
	 {	 tmp_value = dt_addToDateTime(Hours, -24, timeend_day);
	     timestart_day = tmp_value > pwstart ? tmp_value : pwstart;
         timeend = timeend_day;
		 for(day_division = 0; day_division < MAX_DAY_DIVISION; day_division++)
		 {   if(timeend <= pwstart)
				 break;
			 timestart = dt_addToDateTime(Hours, -24/MAX_DAY_DIVISION, timeend);
			 if(timestart < pwstart)
				 timestart = pwstart;
		     strcpy(timestartstr, dt_DateTimeToDateTimeString(timestart, tbuf1, "%Y/%m/%d %H:%M"));                        
			 strcpy(timeendstr, dt_DateTimeToDateTimeString(timeend, tbuf2, "%Y/%m/%d %H:%M"));
             *(paramval + 0) = strdup(timestartstr);
             *(paramval + 1) = strdup(timeendstr);
			 fprintf(logFile, "####From %s to %s\n", timestartstr, timeendstr);
			 for(zoneid = 1; zoneid <= ZONE_NUM; zoneid++)
			 { sprintf(zoneidstr,"%d", zoneid);
			   *(paramval + 2) = strdup(zoneidstr);
			   demandnumbyzoneSQL_query = strdup(demandnumbyzoneSQL);
			   for(x = 0; x < vc; ++x)
			   { paramVal = strdup(*(paramval + x));
			      s1 = substitute(demandnumbyzoneSQL_query, makevar(*(variables + x)), paramVal, &sc);
				  if(!s1) 
				  { logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			        exit(1);
		          }
		          demandnumbyzoneSQL_query = strdup(s1);
		          if(!demandnumbyzoneSQL_query) 
				  {
			       logMsg(logFile,"Out of memory.\n");
			       exit(1);
				  }
			   }
			   //logMsg(logFile, "%s\n", demandnumbyzoneSQL_query);
               if(!myDoQuery(myconn, demandnumbyzoneSQL_query, &res, &cols)) 
			   {
		         logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		         writeWarningData(myconn); exit(1);
	           }
	           rowCount = mysql_num_rows(res);
	           if(! rowCount) 
			   {
		        logMsg(logFile,"readContingencyFkDemand(): 0 rows returned.\n");
		        return(0);
	           }
			   for(rows = 0; rows < rowCount; rows++)
			   { row = mysql_fetch_row(res);
		         if(! row)
			      break;
                 demandnumbyzone[day][day_division][zoneid][dmdnumCJ1] = atoi(row[dmdnumCJ1]);
                 demandnumbyzone[day][day_division][zoneid][dmdnumBRV] = atoi(row[dmdnumBRV]);
                 demandnumbyzone[day][day_division][zoneid][dmdnumCJ3] = atoi(row[dmdnumCJ3]);
				 demandnumbyzone[day][day_division][zoneid][dmdnumEXL] = atoi(row[dmdnumEXL]);
				 demandnumbyzone[day][day_division][zoneid][dmdnumSOV] = atoi(row[dmdnumSOV]);
			   }
			   fprintf(logFile,"Zone %d: %d %d %d %d %d\n",
				   zoneid,
				   demandnumbyzone[day][day_division][zoneid][dmdnumCJ1],
				   demandnumbyzone[day][day_division][zoneid][dmdnumBRV],
                   demandnumbyzone[day][day_division][zoneid][dmdnumCJ3],
				   demandnumbyzone[day][day_division][zoneid][dmdnumEXL],
				   demandnumbyzone[day][day_division][zoneid][dmdnumSOV]); 
			 }
		    logMsg(logFile, "##############################################\n");
		    timeend = timestart;
		 }
		 if(timeend_day <= pwstart)
			break;
		 timeend_day =timestart_day; 
	}*/
	numFakeDemand = totalfkdmdadded;
	//numDemandAllocd = numDemandAllocd + numFakeDemand;
	// free mysql results
	mysql_free_result(res);
	return(0);
}
/****************************************************************************************************
*	Function	addFkDemand							            Date last modified:  03/11/08 Jintao
*	Purpose:	add fake demand element to demandList 
****************************************************************************************************/

static void addFkDemand(int dmdnum, int maxfkdmdnum, int day_division, DateTime timestart, DateTime timeend, int actypeidx, int zoneid, int fkdmdduration , int *totalfkdmdadded)
{   int fkdmdmade;
    int i, j;
	//int totalfkdmdnum;
	//int startfkdmdidx;
	//Demand *dmdptr; 
	Demand *tPtr;
	//int alternate;
	DateTime tmp_start, central_start;
	DateTime tmp_end, central_end;
	char *p;
	int outrunwaylength;
	int inrunwaylength;
	BINTREENODE * tmp;
	AirportLatLon *aptPtr;

	
	//if(dmdnum ==1)
	  //fkdmdmade = 0;
	//else
	  //fkdmdmade = (maxfkdmdnum >= round(dmdnum*1*contingency_prob[actypeidx]))? maxfkdmdnum : round_maxnumdmd(dmdnum*1*contingency_prob[actypeidx]);

	//the one might be working well
	if(dmdnum*1*contingency_prob[actypeidx]<0.46) //0.31/0.4)
        fkdmdmade = 0;
	else 
		fkdmdmade = maxfkdmdnum >= round(dmdnum*1*contingency_prob[actypeidx])? maxfkdmdnum : round(dmdnum*1*contingency_prob[actypeidx]);
	   // totalfkdmdnum = 0;
	
    //test
	/*if(dmdnum*1*contingency_prob[actypeidx]<0.7 && dmdnum < 4)
        fkdmdmade = 0;
	else 
	  {	
		  fkdmdmade = maxfkdmdnum >= round(dmdnum*1*contingency_prob[actypeidx])? maxfkdmdnum : round(dmdnum*1*contingency_prob[actypeidx]);
	    if(dmdnum >= 4 && fkdmdmade == 0)
           fkdmdmade = (maxfkdmdnum >= round(dmdnum*1*contingency_prob[actypeidx]))? maxfkdmdnum : round(dmdnum*1*contingency_prob[actypeidx]);
	  }
    //test*/
    

	if(fkdmdmade>=2)
	  outrunwaylength = 1;
    outrunwaylength = 0;
	inrunwaylength = 0;
	tPtr = NULL;

	if((tPtr = (Demand *) calloc(1, sizeof(Demand))) == NULL) 
	{ logMsg(logFile,"%s Line %d, Out of Memory in addFkDemand().\n", __FILE__,__LINE__);
		   writeWarningData(myconn); exit(1);
	}

	if((aptPtr = (AirportLatLon *) calloc(1, sizeof(AirportLatLon))) == NULL) 
	{ logMsg(logFile,"%s Line %d, Out of Memory in addFkDemand().\n", __FILE__,__LINE__);
		   writeWarningData(myconn); exit(1);
	}
	for(i=1; i <= fkdmdmade; i++)
	{  //if((demandList = (Demand *)realloc((demandList),(numDemand+1)*sizeof(Demand))) == NULL) 
	   //	 { logMsg(logFile,"%s Line %d, Out of Memory in addFkDemand().\n", __FILE__,__LINE__);
	   //	   writeWarningData(myconn); exit(1);
	   //  }

	   
	   demandList[numDemand].contingecnyfkdmdflag = 1;
	   demandList[numDemand].demandID = maxDemandID + 100000 + (*totalfkdmdadded);
	   //if(demandList[numDemandAllocd].demandID > maxDemandID)
	   //		maxDemandID = demandList[numDemandAllocd].demandID;
	   demandList[numDemand].ownerID = 87359; //for citationshares
       //demandList[numDemand].ownerID = 111111; //for citationshares
	   //demandList[numDemand].contractID = 111111; //for citationshares;

	   /*switch(zoneid) {
		case 1: demandList[numDemand].outAirportID = 3453; 
			    demandList[numDemand].outFboID = 1860;
				outrunwaylength = 6548;
			    break; //HPN
		case 2: demandList[numDemand].outAirportID = 1455; 
			    demandList[numDemand].outFboID = 2674;
				outrunwaylength = 13000;
			    break;//ORD chicago intl 
		case 3: demandList[numDemand].outAirportID = 3853; 
			    demandList[numDemand].outFboID = 1819;
				outrunwaylength = 6600;
			    break; //HIO
		case 4: demandList[numDemand].outAirportID = 546; 
			    demandList[numDemand].outFboID = 35226; 
				outrunwaylength = 8249;
			    break;  //SDL 
		case 5: demandList[numDemand].outAirportID = 4291;
			    demandList[numDemand].outFboID = 880; 
				outrunwaylength = 12251;
			    break;  //AUS
		case 6: demandList[numDemand].outAirportID = 1036; 
			    demandList[numDemand].outFboID = 2736;
				outrunwaylength = 7989;
			    break; //PBI
		}*/
	    switch(actypeidx) {
		case 0: demandList[numDemand].contractID = 3246;
			    break;
		case 1: demandList[numDemand].contractID = 3247; 
			    break;
		case 2: demandList[numDemand].contractID = 3248;
			    break; 
		case 3: demandList[numDemand].contractID = 3249;
			    break;  
		case 4: demandList[numDemand].contractID = 3250;
			    break; 
		}
        aptPtr = getAirportLatLonInfoByAptID(hubsAptIDbyZones[zoneid-1][i-1]);
		demandList[numDemand].outAirportID = aptPtr->airportID;
		strcpy(demandList[numDemand].outAptICAO,aptPtr->icao);
		demandList[numDemand].inAirportID = demandList[numDemand].outAirportID;
		strcpy(demandList[numDemand].inAptICAO, aptPtr->icao);
		//demandList[numDemand].inFboID = demandList[numDemand].outFboID;
		//inrunwaylength = outrunwaylength;
        
        // calculate time_t reqOut //
		if(i == 1)
		{   central_start = dt_addToDateTime(Minutes, 1.5*60,  timestart);
		    central_end   = dt_addToDateTime(Minutes, fkdmdduration*60,  central_start);
			tmp_start = central_start;
			tmp_end   = central_end;
		}
		else 
		{ if(day_division == 2)
		  { tmp_end   = (dt_addToDateTime(Minutes, 1*60,  tmp_end) <= timeend)? dt_addToDateTime(Minutes, 1*60,  tmp_end) : timeend;
			tmp_start =  dt_addToDateTime(Minutes, -fkdmdduration*60,  tmp_end);
		  }
		  else if(day_division == 0)
		  { tmp_start   = (dt_addToDateTime(Minutes, -1*60,  tmp_start) >= timestart)? dt_addToDateTime(Minutes, -1*60,  tmp_start) : timestart;
			tmp_end =  dt_addToDateTime(Minutes, fkdmdduration*60,  tmp_start);
		  }
	      else
          {  if((i-1)%2 == 1) 
			 { tmp_start   = (dt_addToDateTime(Minutes, -1*60,  tmp_start) >= timestart)? dt_addToDateTime(Minutes, -1*60,  tmp_start) : timestart;
			   tmp_end =  dt_addToDateTime(Minutes, fkdmdduration*60,  tmp_start);
			 }
			 else
			 { tmp_end   = (dt_addToDateTime(Minutes, 1*60,  tmp_end) <= timeend)? dt_addToDateTime(Minutes, 1*60,  tmp_end) : timeend;
			   tmp_start =  dt_addToDateTime(Minutes, -fkdmdduration*60,  tmp_end);
			 }
		  }
		}
		
		demandList[numDemand].reqOut = DateTimeToTime_t(tmp_start);
		
		// calculate time_t reqIn //
		demandList[numDemand].reqIn  = DateTimeToTime_t(tmp_end);
		//need to think about it.

		demandList[numDemand].contractFlag = 1; //make it flexible;
        //#############################################
		demandList[numDemand].aircraftTypeID  = acTypeList[actypeidx].aircraftTypeID; // Need to confirm
		demandList[numDemand].sequencePosn  = acTypeList[actypeidx].sequencePosn;
		//#############################################
		demandList[numDemand].numPax  = 1;
		
		p = aptIdToIcao(demandList[numDemand].outAirportID);
		if(p)
			strcpy(demandList[numDemand].outAptICAO, p);
		else
			logMsg(logFile,"%s Line %d: airportID %d not found.\n", __FILE__,__LINE__, demandList[numDemand].outAirportID);
		p = aptIdToIcao(demandList[numDemand].inAirportID);
		if(p)
			strcpy(demandList[numDemand].inAptICAO, p);
		else
			logMsg(logFile,"%s Line %d: airportID %d not found.\n", __FILE__,__LINE__, demandList[numDemand].inAirportID);
		demandList[numDemand].outCountryID = 1;
		demandList[numDemand].inCountryID = 1;
		demandList[numDemand].contingencyidx = i;
		demandList[numDemand].demandnuminzone = dmdnum;
		for(j =1; j< MAX_AC_TYPES; j++)
			demandList[numDemand].incRevStatus[j-1] = 20; //tmp fix;

		if ( (outrunwaylength > 0 &&  outrunwaylength < 5000) || (inrunwaylength > 0 && outrunwaylength < 5000) )
			  demandList[numDemand].noCharterFlag = 1;
		//tPtr = &demandList[numDemand];
		tPtr->demandID = demandList[numDemand].demandID; 
		if (!(tmp = TreeSearch(dmdXDemandIdRoot, tPtr, demandIdCompare)))
		   if(!(dmdXDemandIdRoot = RBTreeInsert(dmdXDemandIdRoot, tPtr, demandIdCompare)))
		   { logMsg(logFile,"%s Line %d, Duplicate demandID in readDemandList().\n",__FILE__,__LINE__);
		     exit(1);
		   } 
		   	/*sprintf(writetodbstring1, "%s Line %d, Duplicate demandID in readDemandList().",__FILE__,__LINE__);
		    if(errorNumber==0)
			{ if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		         {logMsg(logFile,"%s Line %d, Out of Memory in readDemandList().\n", __FILE__,__LINE__);
		          writeWarningData(myconn); exit(1);
	             }
			}				
		    else
			{   if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		         {logMsg(logFile,"%s Line %d, Out of Memory in readCrewList().\n", __FILE__,__LINE__);
		          writeWarningData(myconn); exit(1);
	             }
			}
			initializeWarningInfo(&errorinfoList[errorNumber]);
		    errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
            strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
		    sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
		    sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			errorinfoList[errorNumber].format_number=23;
            strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
			errorNumber++;
			writeWarningData(myconn); exit(1);
		}*/

		getPeakDayAdjustment(&demandList[numDemand]); //And recovery adjustment
		numDemand++;
	    (*totalfkdmdadded)++;
	}
 }


int prob_factorial(int n, int k)
{  int i, sum, n1, k1;
   //double sub_prob, prob;

  sum = 1;
  n1 = n;
  k1 = k;
  if(k = 0)
   return sum;
  for(i = 1; i <= k; i++)
    sum = sum*(n-(i-1))/i;
  return(sum);
}

/*
long round_maxnumdmd(double x)
{     assert(x >= LONG_MIN-0.5);
      assert(x <= LONG_MAX+0.5);
      if (x >= 0)
         return (long) (x+0.5);
      return (long) (x-0.5);
}
*/

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

/****************************************************************************************************
*	Function	getctgncyincev							Date last modified:  03/10/08 Jintao
*	Purpose:	get incentive for contingency aircraft by actype and contingency index in an airport 
****************************************************************************************************/

void getctgncyincev(int actypeidx, int contingencyidx, int dmdnum, double *incentive)
{  //double prob_acfail;
	int i;
//	int it;
	double p; 
	double tmp;
	double benefits;
     
	p = contingency_prob[actypeidx];
	tmp = 1.0;
	benefits = 0.0;
    for(i= 1; i<=contingencyidx; i++)  
	{  tmp = tmp - prob_factorial(dmdnum, i-1)*pow(p,i-1)*pow((1-p),dmdnum-i+1);
	}

	for(i = actypeidx; i>=0 ; i--)
	{   benefits =  benefits + acTypeList[i].charterCost * pow(1/2, actypeidx-i);
	}
	(*incentive) = benefits*tmp; 
}

static void readHubsbyZones(void)
{  //zone 1
   hubsAptIDbyZones[0][0] = 3453;
   hubsAptIDbyZones[0][1] = 3178;
   hubsAptIDbyZones[0][2] = 936;
   hubsAptIDbyZones[0][3] = 1978;  
   //zone 2
   hubsAptIDbyZones[1][0] = 1454;
   hubsAptIDbyZones[1][1] = 840;
   hubsAptIDbyZones[1][2] = 1814;
   hubsAptIDbyZones[1][3] = 2533;
   //zone 3
   hubsAptIDbyZones[2][0] = 4692;
   hubsAptIDbyZones[2][1] = 5098;
   hubsAptIDbyZones[2][2] = 1365;
   hubsAptIDbyZones[2][3] = 4795;
   //zone 4
   hubsAptIDbyZones[3][0] = 546;
   hubsAptIDbyZones[3][1] = 3281;
   hubsAptIDbyZones[3][2] = 538;
   hubsAptIDbyZones[3][3] = 773;
   //zone 5
   hubsAptIDbyZones[4][0] = 4641;
   hubsAptIDbyZones[4][1] = 4362;
   hubsAptIDbyZones[4][2] = 4291;
   hubsAptIDbyZones[4][3] = 4577;
   //zone 6
   hubsAptIDbyZones[5][0] = 1110;
   hubsAptIDbyZones[5][1] = 1020;
   hubsAptIDbyZones[5][2] = 1018;
   hubsAptIDbyZones[5][3] = 1030;
}

typedef enum
{ field_crewid=0, field_dpt_aptid, field_arr_aptid, field_travel_dpttm, field_travel_arrtm, field_rqtid
}CsTravelColmns;

/********************************************************************************
*	Function   readCsTraveldata()
*	Purpose:  read in cs_travel_data
********************************************************************************/
static int
readCsTraveldata(MY_CONNECTION *myconn)
{

	extern char *cstravelSQL;
	MYSQL_RES *res;
	MYSQL_FIELD *cols;
    int currcrewid, currrqtid, cs_crewid, rqtid, dpt_aptid, arr_aptid, errNbr;
	DateTime travel_dptTm, travel_arrTm, dt;
	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	CsTravelData *cstrlPtr;


	//test
	int cstravelnodeadded = 0;
	dt = dt_addToDateTime(Hours, optParam.travelcutoff, dt_run_time_GMT); 
    //test

	if(!myDoQuery(myconn, cstravelSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"No commercial travel information read.\n");
		writeWarningData(myconn);
		return(0);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"CS_travel_flight is empty.\n");
		return(0);
	}
#ifdef DEBUGGING
	logMsg(logFile,"raw query results for cstravelSQL:\n");
	if (verbose) displayResults(res, cols);
	mysql_data_seek(res, (my_ulonglong) 0);  // reset pointer into results back to beginning
#endif // DEBUGGING
    if((cstrlPtr = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) 
	{   logMsg(logFile,"%s Line %d, Out of Memory in readCsTraveldata().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}
	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		cs_crewid = atoi(row[field_crewid]);
		rqtid = atoi(row[field_rqtid]);
        dpt_aptid = atoi(row[field_dpt_aptid]);
		arr_aptid = atoi(row[field_arr_aptid]);
        if ((travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[field_travel_dpttm], NULL, &errNbr)) == BadDateTime) {
	    logMsg(logFile, "%s Line %d, Bad date in readCsTraveldata(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[field_travel_dpttm]);
		exit(1);
		}
		if ((travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[field_travel_arrtm], NULL, &errNbr)) == BadDateTime) {
	    logMsg(logFile, "%s Line %d, Bad date in readCsTraveldata(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[field_travel_arrtm]);
		exit(1);
		}
		//if(rows == 0)
		//{ cstrlPtr->crewID = cs_crewid;
	    //  cstrlPtr->rqtid = rqtid;
		//  currcrewid = cs_crewid;
		//  currrqtid = rqtid;
		//}
		if(rows > 0)
		{  if(cstrlPtr->rqtID == rqtid && cstrlPtr->crewID != cs_crewid)
		   {logMsg(logFile, "Same request_id %d with different traveller_id %d and %d in cs_travel_flights.\n", rqtid, cstrlPtr->crewID, cs_crewid);
		    exit(1);
		   }
		   else if(cstrlPtr->rqtID == rqtid && cstrlPtr->crewID == cs_crewid)
		   {  if(cstrlPtr->arr_aptID != dpt_aptid || cstrlPtr->travel_arrTm > travel_dptTm)
		        {logMsg(logFile, "Crew_id %d has invalid travel flight information with request_id %d in cs_travel_flights.\n", cs_crewid, rqtid);
		         exit(1);
		        }
		      cstrlPtr->arr_aptID = arr_aptid;
              cstrlPtr->travel_arrTm = travel_arrTm;
			  continue;
		   }
		   else
		   {   if(cstrlPtr->travel_dptTm <= dt){
			      if(!(travel_flightRoot = RBTreeInsert(travel_flightRoot, cstrlPtr, travelFlightCmp))) {
					 logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCsTraveldata().\n",__FILE__,__LINE__);
					 exit(1);
				  }
			      cstravelnodeadded++;
			   }
		   }
		}
		if((cstrlPtr = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) 
	    {   logMsg(logFile,"%s Line %d, Out of Memory in readCsTraveldata().\n", __FILE__, __LINE__);
		    writeWarningData(myconn); exit(1);
	    }
		cstrlPtr->crewID = cs_crewid;
	    cstrlPtr->rqtID = rqtid;
		cstrlPtr->dpt_aptID = atoi(row[field_dpt_aptid]);
		cstrlPtr->arr_aptID = atoi(row[field_arr_aptid]);
		if ((cstrlPtr->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[field_travel_dpttm], NULL, &errNbr)) == BadDateTime) {
	      logMsg(logFile, "%s Line %d, Bad date in readCsTraveldata(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[field_travel_dpttm]);
		  exit(1);
		}
		if ((cstrlPtr->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[field_travel_arrtm], NULL, &errNbr)) == BadDateTime) {
	      logMsg(logFile, "%s Line %d, Bad date in readCsTraveldata(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[field_travel_arrtm]);
		  exit(1);
		}
	}
	if(rows == rowCount)
	{   if(cstrlPtr->travel_dptTm <= dt){
			if(!(travel_flightRoot = RBTreeInsert(travel_flightRoot, cstrlPtr, travelFlightCmp))) {
					 logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCsTraveldata().\n",__FILE__,__LINE__);
					 exit(1);
					}
			cstravelnodeadded++;
	    }
	}
    // free mysql results
	mysql_free_result(res);
	return 0;

}


/********************************************************************************
*	Function   readCsTraveldataFromOracleDB(ORACLE_SOCKET *orl_socket_cstrl)
*	Purpose:  read in cs_travel_data
********************************************************************************/
static int
readCsTraveldataFromOracleDB(ORACLE_SOCKET *orl_socket_cstrl)
{

	extern char *cstravelOracleSQL;
    int currcrewid, currrqtid, cs_crewid, rqtid, dpt_aptid, arr_aptid, errNbr;
		DateTime travel_dptTm, travel_arrTm, dt;
	my_ulonglong rowCount, rows;
	CsTravelData *cstrlPtr;

	//test
	int cstravelnodeadded = 0;
    //test
    
	dt = dt_addToDateTime(Hours, optParam.travelcutoff, dt_run_time_GMT); 
	if(Orlconnection_doquery(orl_socket_cstrl, cstravelOracleSQL))
	{  logMsg(logFile,"%s Line %d: failed to execute query /*cstravelOracleSQL*/ and get data\n", __FILE__,__LINE__);
	   exit(1);
	} 

    if((cstrlPtr = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) 
	{   logMsg(logFile,"%s Line %d, Out of Memory in readCsTraveldata().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}
	rows = 0;
	rowCount = orl_socket_cstrl->row_count;
	while(Orlconnection_fetch(orl_socket_cstrl)==0) {
		rowCount = OrlConnection_num_rows(orl_socket_cstrl);
		cs_crewid = atoi(orl_socket_cstrl->results[field_crewid]);
		rqtid = atoi(orl_socket_cstrl->results[field_rqtid]);
        dpt_aptid = atoi(orl_socket_cstrl->results[field_dpt_aptid]);
		arr_aptid = atoi(orl_socket_cstrl->results[field_arr_aptid]);
        if ((travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket_cstrl->results[field_travel_dpttm], NULL, &errNbr)) == BadDateTime) {
	    logMsg(logFile, "%s Line %d, Bad date in readCsTraveldata(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, orl_socket_cstrl->results[field_travel_dpttm]);
		exit(1);
		}
		if ((travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket_cstrl->results[field_travel_arrtm], NULL, &errNbr)) == BadDateTime) {
	    logMsg(logFile, "%s Line %d, Bad date in readCsTraveldata(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, orl_socket_cstrl->results[field_travel_arrtm]);
		exit(1);
		}
		//if(rows == 0)
		//{ cstrlPtr->crewID = cs_crewid;
	    //  cstrlPtr->rqtid = rqtid;
		//  currcrewid = cs_crewid;
		//  currrqtid = rqtid;
		//}
		if(rows > 0)
		{  if(cstrlPtr->rqtID == rqtid && cstrlPtr->crewID != cs_crewid)
		   {logMsg(logFile, "Same request_id %d with different traveller_id %d and %d in cs_travel_flights.\n", rqtid, cstrlPtr->crewID, cs_crewid);
		    exit(1);
		   }
		   else if(cstrlPtr->rqtID == rqtid && cstrlPtr->crewID == cs_crewid)
		   {  if(cstrlPtr->arr_aptID != dpt_aptid || cstrlPtr->travel_arrTm > travel_dptTm)
		        {logMsg(logFile, "Crew_id %d has invalid travel flight information with request_id %d in cs_travel_flights.\n", cs_crewid, rqtid);
				  rows++;
				  continue;
		         //exit(1); //NO EXIT FOR PRODUCTION VERSION
		        }
		      cstrlPtr->arr_aptID = arr_aptid;
              cstrlPtr->travel_arrTm = travel_arrTm;
			  rows++;
			  continue;
		   }
		   else
		    {   if(cstrlPtr->travel_dptTm <= dt){
			      if(!(travel_flightRoot = RBTreeInsert(travel_flightRoot, cstrlPtr, travelFlightCmp))) {
					 logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCsTraveldata().\n",__FILE__,__LINE__);
					 exit(1);
				  }
			      cstravelnodeadded++;
			   }
		   }
		}
		if((cstrlPtr = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) 
	    {   logMsg(logFile,"%s Line %d, Out of Memory in readCsTraveldata().\n", __FILE__, __LINE__);
		    writeWarningData(myconn); exit(1);
	    }
		cstrlPtr->crewID = cs_crewid;
	    cstrlPtr->rqtID = rqtid;
		cstrlPtr->dpt_aptID = atoi(orl_socket_cstrl->results[field_dpt_aptid]);
		cstrlPtr->arr_aptID = atoi(orl_socket_cstrl->results[field_arr_aptid]);
		if ((cstrlPtr->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket_cstrl->results[field_travel_dpttm], NULL, &errNbr)) == BadDateTime) {
	      logMsg(logFile, "%s Line %d, Bad date in readCsTraveldata(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, orl_socket_cstrl->results[field_travel_dpttm]);
		  exit(1);
		}
		if ((cstrlPtr->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", orl_socket_cstrl->results[field_travel_arrtm], NULL, &errNbr)) == BadDateTime) {
	      logMsg(logFile, "%s Line %d, Bad date in readCsTraveldata(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, orl_socket_cstrl->results[field_travel_arrtm]);
		  exit(1);
		}
		rows++;
	}
	//if(rows == rowCount){   
	if(cstrlPtr->travel_dptTm <= dt){
			if(!(travel_flightRoot = RBTreeInsert(travel_flightRoot, cstrlPtr, travelFlightCmp))) {
					 logMsg(logFile,"%s Line %d, RBTreeInsert() failed in readCsTraveldata().\n",__FILE__,__LINE__);
					 exit(1);
					}
			cstravelnodeadded++;
	}
	//}
    // free mysql results
	return 0;
}


static int
travelFlightCmp(void *a1, void *b1)
{
	CsTravelData *a = (CsTravelData *) a1;
	CsTravelData *b = (CsTravelData *) b1;
	int ret;

	
	if(ret = a->crewID - b->crewID)
		return(ret);
	if(ret = a->travel_dptTm - b->travel_dptTm)
		return(ret);
	if(ret = a->travel_arrTm - b->travel_arrTm)
		return(ret);
	if(ret = a->dpt_aptID - b->dpt_aptID)
		return(ret);
    if(ret = a->arr_aptID - b->arr_aptID)
        return(ret);
    if(ret = a->rqtID - b->rqtID)
        return(ret);
	return(0);
}
/****************************************************************************************************
*	Function	getDutySoFarWithCTC01							Date last modified:  29/05/08 Jintao, RLZ 09/18/2008
*	Purpose:	New Version of getdutySoFar01() with CTC data integrated
****************************************************************************************************/

static int
getDutySoFarWithCTC01(PRE_Crew *preCrewPtr)
{
	RawAircraftData *radLastLegPtr;
	CsTravelData *cstrlPtr;
	DateTime dt_pwStart, dt_runTime, flight_in_time, on2Info, off2Info;
	DateTime cstrl_arrTm, cstrl_dptTm;
	int tPreFltTm = 0;
	int days, hours, minutes, seconds, msecs;
	int dutytime;
	int updateLoc = 0;
	int cstrl_ignored = 0;
	//BINTREENODE *tmp;
//	CrewData *cdptr;

	//if(! preCrewPtr->availAirportID)
	//	preCrewPtr->availAirportID = preCrewPtr->startLoc;

	dt_pwStart = dt_time_tToDateTime(optParam.windowStart);
	dt_runTime = dt_run_time_GMT;

	radLastLegPtr = preCrewPtr->lastActivityLeg;
    cstrlPtr = preCrewPtr->lastCsTravelLeg;
	preCrewPtr->inTourTransfer = 0; //initialize

	if(cstrlPtr)
	{ cstrl_arrTm = dt_addToDateTime(Minutes, optParam.postArrivalTime, cstrlPtr->travel_arrTm);
	  cstrl_dptTm = dt_addToDateTime(Minutes, -optParam.preBoardTime, cstrlPtr->travel_dptTm);
	}
	else
      cstrl_arrTm = 0;

	if(radLastLegPtr && cstrlPtr){  
		//Travel after pws and after a leg, ignore travel
		if(strcmp(radLastLegPtr->rowtype,"logmgdleg") == 0 || strcmp(radLastLegPtr->rowtype,"mgdleg") == 0){
			if((cstrl_arrTm >= radLastLegPtr->rec_intime) && (cstrl_dptTm > dt_pwStart)){
				//cstrlPtr = NULL;
				cstrl_ignored = 1; 
			}
		}
		//Travel after pws and  lastActLeg is short MX, ignore travel
		if(strcmp(radLastLegPtr->rowtype,"mtcnote") == 0 || strcmp(radLastLegPtr->rowtype,"ownersign") == 0){
			if(cstrl_dptTm > dt_pwStart &&
				(radLastLegPtr->rec_intime <= dt_addToDateTime(Minutes, optParam.maintTmForReassign, dt_pwStart))){
			//	cstrlPtr = NULL;
				cstrl_ignored = 1;
			}
		}	
	}

//	if(radLastLegPtr&&(! cstrlPtr||(radLastLegPtr->rec_intime >= cstrl_arrTm)&&(strcmp(radLastLegPtr->rowtype,"logmgdleg") == 0 || strcmp(radLastLegPtr->rowtype,"mgdleg") == 0))){
	//RLZ: same as the case without travel

	//I think the second condition is already excluded above.
	//	||((radLastLegPtr->rec_outtime < dt_pwStart) && (radLastLegPtr->rec_outtime >= dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm))
	//	&& cstrl_dptTm > dt_pwStart))
	//&&(strcmp(radLastLegPtr->rowtype,"logmgdleg") == 0 || strcmp(radLastLegPtr->rowtype,"mgdleg") == 0))
	if(radLastLegPtr&&
		(! cstrlPtr || cstrl_ignored || (radLastLegPtr->rec_intime >= cstrl_arrTm))){
		if(strcmp(radLastLegPtr->rowtype,"logmgdleg") == 0 || strcmp(radLastLegPtr->rowtype,"mgdleg") == 0){
			//the last leg is a flight leg
			if(!preCrewPtr->on1 && verbose){
				fprintf(logFile, "\n Found crew with lastActivityLeg = 'logmgdleg' or 'mgdleg' but no (ON1,OFF1) information: \n");
				fprintf(logFile, "ErrorID = 0, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
				fprintf(logFile, "\n");
			}//end if
			preCrewPtr->availAirportID = radLastLegPtr->inaptid;
			if((radLastLegPtr->rec_outtime < dt_pwStart) && (radLastLegPtr->rec_intime >= dt_pwStart)){
				//flight start time < planning window start < flight end time
				preCrewPtr->cscd_availDT = radLastLegPtr->rec_intime;
				preCrewPtr->cscd_canStartLater = 0;
				dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
				dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				preCrewPtr->dutyTime=dutytime;
				preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
			}//end if
			else if((radLastLegPtr->rec_intime < dt_pwStart) && (radLastLegPtr->rec_outtime >= dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm))){
				//means: flight in time < planning window start 
				flight_in_time = radLastLegPtr->rec_intime;
				if((preCrewPtr->on1 < dt_pwStart) && (dt_pwStart <= preCrewPtr->off1)){
					//pilot will be on duty at the start of planning window
					if(flight_in_time > preCrewPtr->on1){
						//last flight leg and planning window start are in the same duty period
						preCrewPtr->cscd_availDT = Max(flight_in_time, 
													   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));
						//preCrewPtr->cscd_availDT = Max(flight_in_time, 
						//							   Max(dt_addToDateTime(Minutes, -optParam.preFlightTm, dt_pwStart), 
						//								   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime)));
						//preCrewPtr->cscd_availDT = Max(flight_in_time, 
						//							   dt_addToDateTime(Minutes, -optParam.preFlightTm, dt_pwStart));// DEBUG
						preCrewPtr->cscd_canStartLater = 0;
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			            dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				        preCrewPtr->dutyTime=dutytime;
                        preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);

					}//end if
					else {
						//last flight leg and planning window start are in different duty periods - depends on run start time
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -optParam.firstPreFltTm, dt_pwStart), 
														Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime), 
															preCrewPtr->on1));
						preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) < 
														  dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on1)) ? 1 : 0;
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
						dutytime = (24 * 60 * days) + (60 * hours) + minutes;
						preCrewPtr->dutyTime=dutytime;
						preCrewPtr->blockTime=0;
					}//end else
				}//end if
				else {
					//pilot will be resting at the start of planning window
					preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1));
					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <  preCrewPtr->off1) ? 1 : 0;
					preCrewPtr->blockTime=0;
					preCrewPtr->dutyTime=0;
				}//end else
			}//end elseif
			else {
				//new 'Flight Leg' case - Display Warning
				if(verbose){
					fprintf(logFile, "\n Found error(s) for crew whose lastActivityLeg is a mgdleg or logmgdleg: \n");
					fprintf(logFile, "ErrorID = 1, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
					fprintf(logFile, "\n");
				}//end if
			}//end else
		}//end if
		
		else if(strcmp(radLastLegPtr->rowtype,"mtcnote") == 0 || strcmp(radLastLegPtr->rowtype,"ownersign") == 0)
		{
			if(!preCrewPtr->on1 && !preCrewPtr->on2 && verbose){
				fprintf(logFile, "\n Found crew with lastActivityLeg = 'mtcnote' or 'ownersign' but no (ON1,OFF1) and (ON2,OFF2) information: \n");
				fprintf(logFile, "ErrorID = 0, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
				fprintf(logFile, "\n");
			}//end if			//the last leg is a maintenance or an ownersigning
			preCrewPtr->availAirportID = radLastLegPtr->inaptid;
			if((radLastLegPtr->rec_outtime <= dt_pwStart) && 
				 (radLastLegPtr->rec_intime >= dt_pwStart) &&
				 (radLastLegPtr->rec_intime <= dt_addToDateTime(Minutes, optParam.maintTmForReassign, dt_pwStart)))
			{ //MX case #1A
				on2Info = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1));
				off2Info = (preCrewPtr->on2) ? (preCrewPtr->off2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.maxDutyTm, preCrewPtr->off1));
				if(((radLastLegPtr->rec_intime >= preCrewPtr->on1 && radLastLegPtr->rec_intime <= preCrewPtr->off1) || 
				   (radLastLegPtr->rec_intime >= preCrewPtr->on2 && radLastLegPtr->rec_intime <= preCrewPtr->off2)) &&
				   (radLastLegPtr->rec_intime <= dt_addToDateTime(Hours, 24*(int)preCrewPtr->stayLate, preCrewPtr->tourEndTm)))
				{ 
					//that is, if ((mx.end in (on1, off1) || mx.end in (on2, off2)) AND (mx.end < tourEndTm+stayLate))
					//pilot will be on duty at the end of MX 
					preCrewPtr->cscd_canStartLater = 0;

					tPreFltTm = (preCrewPtr->firstFlightOfTour) ?
								((preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart) ? optParam.preFlightTm : optParam.firstPreFltTm) :
								(optParam.firstPreFltTm);
			    
					if(radLastLegPtr->rec_intime >= preCrewPtr->on1 && radLastLegPtr->rec_intime <= preCrewPtr->off1)
					{ 
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -tPreFltTm, radLastLegPtr->rec_intime), preCrewPtr->on1);
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
					}
					if(radLastLegPtr->rec_intime >= preCrewPtr->on2 && radLastLegPtr->rec_intime <= preCrewPtr->off2)
					{ 
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -tPreFltTm, radLastLegPtr->rec_intime), preCrewPtr->on2);
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on2, &days, &hours, &minutes, &seconds, &msecs);
					}
					dutytime = (24 * 60 * days) + (60 * hours) + minutes;
					preCrewPtr->dutyTime=dutytime;

			        if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else 
						preCrewPtr->blockTime=0;				
				}
				else if((radLastLegPtr->rec_intime >= preCrewPtr->off1) && (radLastLegPtr->rec_intime <= on2Info) &&
						(radLastLegPtr->rec_intime <= dt_addToDateTime(Hours, 24*(int)preCrewPtr->stayLate, preCrewPtr->tourEndTm)))
				{
					//pilot will be resting at the end of MX, but end of MX is between off1 and on2
					preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1));
					//Depending on pwS, we might or might not need to consider run start time
					if(dt_pwStart <= preCrewPtr->off1){
						//Don't need to consider run start time
						preCrewPtr->cscd_canStartLater = 1;
					}//end if
					else {
						//Do need to consider run start time
						preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1) ? 1 : 0;
					}//end else
					preCrewPtr->blockTime=0;
					preCrewPtr->dutyTime=0;
				}//end else
				else if((radLastLegPtr->rec_intime >= off2Info)&&
						(radLastLegPtr->rec_intime < dt_addToDateTime(Hours, 24*(int)preCrewPtr->stayLate, preCrewPtr->tourEndTm)))
				{
					//pilot will be resting at the end of MX, but end of MX is bigger than off2
					//This is not suppose to happen (or at least, very rarely)
					//Here we do NOT need to consider run start time - always have enough time to notify
					preCrewPtr->cscd_canStartLater = 1;
					preCrewPtr->cscd_availDT = dt_addToDateTime(Minutes, optParam.minRestTm, off2Info);
                    preCrewPtr->blockTime=0;
					preCrewPtr->dutyTime=0;
				}//end elseif
				else 
					preCrewPtr->availDuringPlanningWindow=0;
			}//end if MX case #1A

			else if((radLastLegPtr->rec_outtime <= dt_pwStart) && 
				    (radLastLegPtr->rec_intime >= dt_pwStart) &&
				    (radLastLegPtr->rec_intime > dt_addToDateTime(Minutes, optParam.maintTmForReassign, dt_pwStart)))
                { //MX case #1B 
				if((preCrewPtr->on1 <= dt_pwStart) &&
				   (preCrewPtr->off1 >= dt_pwStart))
				{
					//pilot will be on duty at the start of planning window
					preCrewPtr->cscd_canStartLater = 0;

					tPreFltTm = (preCrewPtr->firstFlightOfTour) ?
								((preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart) ? optParam.preFlightTm : optParam.firstPreFltTm) :
								(optParam.firstPreFltTm);

					//preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
					//							   dt_addToDateTime(Minutes, -tPreFltTm, dt_pwStart));
//RLZ replaced the above 06202008
					preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
										 dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));
		
						                       
				    dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			        dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				    preCrewPtr->dutyTime=dutytime;

			        if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else 
						preCrewPtr->blockTime=0;
				}//end if
				else 
				{
					//pilot will be resting at the start of planning window
					preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.minTimeToNotify, preCrewPtr->off1));
					//Do need to consider run start time to determine canStartLater	
					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1) ? 1 : 0;
					preCrewPtr->dutyTime=0;
			        if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else 
						preCrewPtr->blockTime=0;
				}//end else
			}//end elseif MX case #1B
			else if(radLastLegPtr->rec_intime < dt_pwStart)
			{ //MX case #2
				tPreFltTm = (preCrewPtr->firstFlightOfTour) ?
							((preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart) ? optParam.preFlightTm : optParam.firstPreFltTm) :
							(optParam.firstPreFltTm);

				if((preCrewPtr->on1 <= dt_pwStart)&&(preCrewPtr->off1 >= dt_pwStart))
				{
					//pilot will be on duty at the start of planning window
					//preCrewPtr->cscd_canStartLater = 0;
				//	preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
				//								   dt_addToDateTime(Minutes, -tPreFltTm, dt_pwStart)); 

					preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
						dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));


					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) < 
									dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on1)) ? 1 : 0;
				    dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			        dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				    preCrewPtr->dutyTime=dutytime;
					if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else
						preCrewPtr->blockTime=0;						   
				}//end if
				else {
					//pilot will be resting at the start of planning window
					//Do need to consider run start time
					if(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1){
						preCrewPtr->cscd_canStartLater = 1;
						preCrewPtr->cscd_availDT = dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1);
						preCrewPtr->dutyTime=0;
						preCrewPtr->blockTime=0;
					}//end if
					else{
						preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.minTimeToNotify, preCrewPtr->off1));
						preCrewPtr->cscd_canStartLater = (preCrewPtr->on2) ? 0 : 1;
						preCrewPtr->dutyTime=0;
						preCrewPtr->blockTime=0;
					}//end else																						
				}//end else
			}//end elseif MX case #2
			else 
			{
				//new MX case - Display Warning
				if(verbose)
				{
					fprintf(logFile, "\n Found error(s) for crew whose lastActivityLeg is a 'mtcnote' or 'ownersign': \n");
					fprintf(logFile, "ErrorID = 2, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
					fprintf(logFile, "\n");
				}//end if
			}//end else
		}//end else if
		
		else {
			//rowtype is other than logmgdleg, mgdleg, mcnote, ownersign - Display Warning
			if(verbose){
				fprintf(logFile, "\n Found error for crew whose lastActivityLeg is not one of these: 'mgdleg', 'logmgdleg', 'mtcnote', 'ownersign' \n");
				fprintf(logFile, "ErrorID = 3, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
				fprintf(logFile, "\n");
			}//end if
			return(0);
		}
		
		//lastly: check if pilot is available during the planning window
		if (dt_addToDateTime(Minutes, optParam.finalPostFltTm, preCrewPtr->cscd_availDT) > dt_addToDateTime(Hours, 24.*preCrewPtr->stayLate, preCrewPtr->tourEndTm)){
			preCrewPtr->availDuringPlanningWindow = 0;
			return(0);
		}
	}//end if radLastLegPtr
	else if (!radLastLegPtr&&!cstrlPtr ) 
	{
		//there is no lastLeg information from lastActivityLeg
		if(preCrewPtr->firstFlightOfTour && preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart )
		{
			//lastActivityLeg is not identified but firstFlightOfTour is defined - Error, Display Warning
			if(verbose)
			{
				fprintf(logFile, "\n Found crew with lastActivityLeg but no firstFlightOfTour: \n");
				fprintf(logFile, "ErrorID = 4, CrewID = %s, lastActivityLeg is not identified but firstFlightOfTour is defined", preCrewPtr->employeenumber);
				fprintf(logFile, "\n");
			}//end if
		}//end if 
		else{
			//this is the 'tour start' case
			preCrewPtr->availAirportID = preCrewPtr->startLoc;
			updateLoc = 1;
			if((preCrewPtr->on1) && (dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm) <= dt_pwStart)){  
				//pilot has started tour at the start of planning window and on1 information is available
				if(preCrewPtr->on1 <= dt_pwStart && dt_pwStart <= preCrewPtr->off1){
					//pilot will be on duty at the start of planning window
					preCrewPtr->cscd_canStartLater = 0;
					preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime),
						                           dt_addToDateTime(Minutes, -optParam.firstPreFltTm, dt_pwStart)));
					dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			        dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				    preCrewPtr->dutyTime=dutytime;
					preCrewPtr->blockTime=0;
				    
				}//end if
				else {
					//pilot will be resting at the start of planning window
					//Do need to consider run start time
					if(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1){
						preCrewPtr->cscd_canStartLater = 1;
						preCrewPtr->cscd_availDT = dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1);
					}//end if
					else{
						preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.minTimeToNotify, preCrewPtr->off1));
						preCrewPtr->cscd_canStartLater = (preCrewPtr->on2) ? 0 : 1;
					}//end else																											
					preCrewPtr->dutyTime=0;
					preCrewPtr->blockTime=0;
				}//end else
			}//end if
			else if((dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm) <= dt_pwStart)){  
				//pilot has started tour at the start of planning window but on1 information is not available
				preCrewPtr->cscd_canStartLater = 1;
				preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, optParam.minTmToTourStart, dt_runTime),
				                           Max(dt_addToDateTime(Minutes, -optParam.firstPreFltTm, dt_pwStart),
										       dt_addToDateTime(Minutes, -24*60*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm)));
				preCrewPtr->dutyTime=0;
				preCrewPtr->blockTime=0;				    				
			}//end elseif
			else {
				//pilot has not started tour at the start of planning window (therefore on1 is not available)
				preCrewPtr->cscd_canStartLater = 1;
				if(preCrewPtr->on2)
				{
					preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm),
											       Min(dt_addToDateTime(Minutes, optParam.minTmToTourStart, dt_runTime),
												       preCrewPtr->on2));
				}//end if
				else 
				{
					preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm),
												   dt_addToDateTime(Minutes, optParam.minTmToTourStart, dt_runTime));
				}//end else
				preCrewPtr->dutyTime=0;//need to change, later
				preCrewPtr->blockTime=0;
			}//end else
		}//end else
	}//end else radLastLegPtr  
	//else((radLastLegPtr && cstrlPtr && radLastLegPtr->rec_intime < cstrl_arrTm)||(!radLastLegPtr && cstrlPtr))
	//leg followed by a Travel leg or only the travel leg
	else if ( !preCrewPtr->on1 && !preCrewPtr->on2 && !preCrewPtr->off1 && !preCrewPtr->off2){
        //on1, off1, on2, off2 are all null //travel is the only activity.
		    preCrewPtr->availAirportID = cstrlPtr->arr_aptID;
			preCrewPtr->hasFlown = 0; //it should be already 0 for this case.
			dt_dateTimeDiff(cstrl_dptTm, cstrl_arrTm, &days, &hours, &minutes, &seconds, &msecs);
            dutytime = (24 * 60 * days) + (60 * hours) + minutes;
			if(dutytime > optParam.crewDutyAfterCommFlt*60){
                preCrewPtr->cscd_availDT = max(dt_addToDateTime(Minutes, optParam.minRestTm, cstrl_arrTm),
					dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));
				preCrewPtr->cscd_canStartLater = 1;
                preCrewPtr->dutyTime = 0;
                preCrewPtr->blockTime= 0;
			}
			else{
				preCrewPtr->cscd_availDT = Max(cstrl_arrTm, 
					                   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime)); //no need for pws - preFlightTm
                preCrewPtr->cscd_canStartLater = 0;
                dt_dateTimeDiff(preCrewPtr->cscd_availDT, cstrl_dptTm, &days, &hours, &minutes, &seconds, &msecs);
                dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				if (dutytime > optParam.maxDutyTm){  //flight was too far away from the runtime
					dutytime = 0;
					preCrewPtr->cscd_canStartLater = 1;
				}
				preCrewPtr->dutyTime =dutytime;
                preCrewPtr->blockTime= 0;
			}
	} 
	    else { // lastActivityLeg followed by Travel leg, can assume on1 and on2 are there.
			preCrewPtr->availAirportID = cstrlPtr->arr_aptID;
			preCrewPtr->hasFlown = 0; //For the case when crew changes ac in the middle of his tour.
            // The travel is for getting crew to home
			// arrLoc == endLoc
			// and The travel time is near the end of the tourEndTm.

			if (cstrlPtr->arr_aptID == preCrewPtr->endLoc 
				&&  cstrl_arrTm >= dt_addToDateTime(Hours, -24, preCrewPtr->tourEndTm)) {
					preCrewPtr->availDuringPlanningWindow = 0;
					//return(0); //Still want to populate availDT information, just in case we want to use this crew - 12/04/08 ANG
				}

			if((cstrl_dptTm < dt_pwStart) && (cstrl_arrTm >= dt_pwStart)){
					//commercial flight start time < planning window start < flight end time, i.e crew is on commercial flight at the planning window start
					preCrewPtr->cscd_availDT = cstrl_arrTm;
					preCrewPtr->cscd_canStartLater = 0;
					// a little redundent but safe for duty time calucation
					if(cstrl_arrTm <= preCrewPtr->off1 && cstrl_dptTm >=preCrewPtr->on1)
					dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
					else if(cstrl_arrTm <= preCrewPtr->off2 && (cstrl_dptTm >= preCrewPtr->on2)) 
					dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on2, &days, &hours, &minutes, &seconds, &msecs);
					else 
					dt_dateTimeDiff(preCrewPtr->cscd_availDT, cstrl_dptTm, &days, &hours, &minutes, &seconds, &msecs);
					dutytime = (24 * 60 * days) + (60 * hours) + minutes;
					preCrewPtr->dutyTime=dutytime;
					preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					//RLZ inTourTranfer flag 03/06/2009, used to get rid of post flight time after a commercial travel
					if (preCrewPtr->blockTime)
						preCrewPtr->inTourTransfer = 1;

				}//end if
			//&& (cstrl_dptTm >= dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm)
			else if (cstrl_arrTm < dt_pwStart){
					//commerical flight in time < planning window start 
					if((preCrewPtr->on1 < dt_pwStart) && (dt_pwStart <= preCrewPtr->off1)){
						//pilot will be on duty at the start of planning window
						if(cstrl_arrTm > preCrewPtr->on1){
							//last commercial flight leg and planning window start are in the same duty period
							preCrewPtr->cscd_availDT = Max(cstrl_arrTm, 
														dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));
							//preCrewPtr->cscd_availDT = Max(cstrl_arrTm, 
							//	Max(dt_addToDateTime(Minutes, -((preCrewPtr->hasFlown)? optParam.preFlightTm:optParam.firstPreFltTm), dt_pwStart), 
							//								   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime)));
							preCrewPtr->cscd_canStartLater = 0;
							dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
							dutytime = (24 * 60 * days) + (60 * hours) + minutes;
							preCrewPtr->dutyTime=dutytime;
							preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
							//RLZ inTourTranfer flag 03/06/2009, used to get rid of post flight time after a commercial travel
							if (preCrewPtr->blockTime)
								preCrewPtr->inTourTransfer = 1;

						}//end if
						else {
							//last commercial flight leg and planning window start are in different duty periods - depends on run start time
							//still can handle a case that "travel is supposed to be in on1 and off1, but data did not reflect that."
							preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -((preCrewPtr->hasFlown)? optParam.preFlightTm:optParam.firstPreFltTm), dt_pwStart), 
															Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime), 
																preCrewPtr->on1));
							preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) < 
															dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on1)) ? 1 : 0;
							dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
							dutytime = (24 * 60 * days) + (60 * hours) + minutes;
							preCrewPtr->dutyTime=dutytime;
							preCrewPtr->blockTime=0;
						}//end else
					}//end if
					else {
						//pilot will be resting at the start of planning window
						preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1? preCrewPtr->off1:cstrl_arrTm));
						preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <  (preCrewPtr->off1? preCrewPtr->off1:(preCrewPtr->on2?dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on2):cstrl_arrTm)))? 1 : 0;
						preCrewPtr->blockTime=0;
						preCrewPtr->dutyTime=0;					
					}//end else
				}//end elseif
				else {
					//commerical flight departes after pws, but arrival time is still within one day of run time
					//these are: 
					//(1) the long MX case, which we honor the flight travel
					//(2) no lastacctivityleg, only travel leg
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime), cstrl_arrTm);
						preCrewPtr->cscd_canStartLater = 0; //(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <  cstrl_dptTm) ? 1 : 0;
						preCrewPtr->blockTime=0;
						if((cstrl_arrTm <= preCrewPtr->off1) && cstrl_dptTm >=preCrewPtr->on1)
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
						else if((cstrl_arrTm <= preCrewPtr->off2) && (cstrl_dptTm >= preCrewPtr->on2)) 
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on2, &days, &hours, &minutes, &seconds, &msecs);
						else 
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, cstrl_dptTm, &days, &hours, &minutes, &seconds, &msecs);
						dutytime = (24 * 60 * days) + (60 * hours) + minutes;
						preCrewPtr->dutyTime = dutytime;
					//if(verbose){
					//	fprintf(logFile, "\n Found error(s) for crew whose lastleg is commercial leg: \n");
					//	fprintf(logFile, "ErrorID = 1, employnumber = %s, lastcommericalleg.dpt_aptID = %d, lastcommericalleg.arr_aptID = %d", preCrewPtr->employeenumber, cstrlPtr->dpt_aptID, cstrlPtr->arr_aptID);
					//	fprintf(logFile, "\n");
					//}//end if
				}//end else 
	}
	if ( preCrewPtr->on2 && optParam.estimateCrewLoc && updateLoc && preCrewPtr->assignedACID && preCrewPtr->AC_AirportID_beforePW && dt_addToDateTime(Hours, -12 - 24.*preCrewPtr->startEarly, preCrewPtr->tourStartTm) < dt_pwStart) 
		//This crew has crewassignment in BW, we update its location same as the AC. RLZ 01/15/2008
         preCrewPtr->availAirportID = preCrewPtr->AC_AirportID_beforePW;
	if(! preCrewPtr->availAirportID)
		preCrewPtr->availAirportID = preCrewPtr->startLoc;

	return(0);
}//end of function


/****************************************************************************************************
*	Function	getDutySoFarWithCTC							Date last modified:  29/05/08 Jintao
*	Purpose:	New Version of getdutySoFar01() with CTC data integrated
****************************************************************************************************/

static int
getDutySoFarWithCTC(PRE_Crew *preCrewPtr) /*Not currently used, a newer version getDutySoFarWithCTC01 is called*/
{
	RawAircraftData *radLastLegPtr;
	CsTravelData *cstrlPtr;
	DateTime dt_pwStart, dt_runTime, flight_in_time, on2Info, off2Info;
	DateTime cstrl_arrTm, cstrl_dptTm;
	int tPreFltTm = 0;
	int days, hours, minutes, seconds, msecs;
	int dutytime;
	int updateLoc = 0;
	int cstrl_ignored = 0;
	//BINTREENODE *tmp;
//	CrewData *cdptr;

	//if(! preCrewPtr->availAirportID)
	//	preCrewPtr->availAirportID = preCrewPtr->startLoc;

	dt_pwStart = dt_time_tToDateTime(optParam.windowStart);
	dt_runTime = dt_run_time_GMT;

	radLastLegPtr = preCrewPtr->lastActivityLeg;
    cstrlPtr = preCrewPtr->lastCsTravelLeg;
	if(cstrlPtr)
	{ cstrl_arrTm = dt_addToDateTime(Minutes, optParam.postArrivalTime, cstrlPtr->travel_arrTm);
	  cstrl_dptTm = dt_addToDateTime(Minutes, -optParam.preBoardTime, cstrlPtr->travel_dptTm);
	}
	else
      cstrl_arrTm = 0;

	if(radLastLegPtr && cstrlPtr){  
		//Travel after pws and after a leg, ignore travel
		if(strcmp(radLastLegPtr->rowtype,"logmgdleg") == 0 || strcmp(radLastLegPtr->rowtype,"mgdleg") == 0){
			if((cstrl_arrTm >= radLastLegPtr->rec_intime) && (cstrl_dptTm > dt_pwStart)){
				//cstrlPtr = NULL;
				cstrl_ignored = 1;
			}
		}
		//Travel after pws and  lastActLeg is short MX, ignore travel
		if(strcmp(radLastLegPtr->rowtype,"mtcnote") == 0 || strcmp(radLastLegPtr->rowtype,"ownersign") == 0){
			if(cstrl_dptTm > dt_pwStart &&
				(radLastLegPtr->rec_intime <= dt_addToDateTime(Minutes, optParam.maintTmForReassign, dt_pwStart))){
			//	cstrlPtr = NULL;
				cstrl_ignored = 1;
			}
		}	
	}





//	if(radLastLegPtr&&(! cstrlPtr||(radLastLegPtr->rec_intime >= cstrl_arrTm)&&(strcmp(radLastLegPtr->rowtype,"logmgdleg") == 0 || strcmp(radLastLegPtr->rowtype,"mgdleg") == 0))){
	//RLZ: same as the case without travel

	//I think the second condition is already excluded above.
	//	||((radLastLegPtr->rec_outtime < dt_pwStart) && (radLastLegPtr->rec_outtime >= dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm))
	//	&& cstrl_dptTm > dt_pwStart))
	//&&(strcmp(radLastLegPtr->rowtype,"logmgdleg") == 0 || strcmp(radLastLegPtr->rowtype,"mgdleg") == 0))
	if(radLastLegPtr&&
		(! cstrlPtr || cstrl_ignored || (radLastLegPtr->rec_intime >= cstrl_arrTm))){
		if(strcmp(radLastLegPtr->rowtype,"logmgdleg") == 0 || strcmp(radLastLegPtr->rowtype,"mgdleg") == 0){
			//the last leg is a flight leg
			if(!preCrewPtr->on1 && verbose){
				fprintf(logFile, "\n Found crew with lastActivityLeg = 'logmgdleg' or 'mgdleg' but no (ON1,OFF1) information: \n");
				fprintf(logFile, "ErrorID = 0, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
				fprintf(logFile, "\n");
			}//end if
			preCrewPtr->availAirportID = radLastLegPtr->inaptid;
			if((radLastLegPtr->rec_outtime < dt_pwStart) && (radLastLegPtr->rec_intime >= dt_pwStart)){
				//flight start time < planning window start < flight end time
				preCrewPtr->cscd_availDT = radLastLegPtr->rec_intime;
				preCrewPtr->cscd_canStartLater = 0;
				dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
				dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				preCrewPtr->dutyTime=dutytime;
				preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
			}//end if
			else if((radLastLegPtr->rec_intime < dt_pwStart) && (radLastLegPtr->rec_outtime >= dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm))){
				//means: flight in time < planning window start 
				flight_in_time = radLastLegPtr->rec_intime;
				if((preCrewPtr->on1 < dt_pwStart) && (dt_pwStart <= preCrewPtr->off1)){
					//pilot will be on duty at the start of planning window
					if(flight_in_time > preCrewPtr->on1){
						//last flight leg and planning window start are in the same duty period
						preCrewPtr->cscd_availDT = Max(flight_in_time, 
													   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));
						//preCrewPtr->cscd_availDT = Max(flight_in_time, 
						//							   Max(dt_addToDateTime(Minutes, -optParam.preFlightTm, dt_pwStart), 
						//								   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime)));
						//preCrewPtr->cscd_availDT = Max(flight_in_time, 
						//							   dt_addToDateTime(Minutes, -optParam.preFlightTm, dt_pwStart));// DEBUG
						preCrewPtr->cscd_canStartLater = 0;
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			            dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				        preCrewPtr->dutyTime=dutytime;
                        preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);

					}//end if
					else {
						//last flight leg and planning window start are in different duty periods - depends on run start time
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -optParam.firstPreFltTm, dt_pwStart), 
														Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime), 
															preCrewPtr->on1));
						preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) < 
														  dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on1)) ? 1 : 0;
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
						dutytime = (24 * 60 * days) + (60 * hours) + minutes;
						preCrewPtr->dutyTime=dutytime;
						preCrewPtr->blockTime=0;
					}//end else
				}//end if
				else {
					//pilot will be resting at the start of planning window
					preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1));
					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <  preCrewPtr->off1) ? 1 : 0;
					preCrewPtr->blockTime=0;
					preCrewPtr->dutyTime=0;
				}//end else
			}//end elseif
			else {
				//new 'Flight Leg' case - Display Warning
				if(verbose){
					fprintf(logFile, "\n Found error(s) for crew whose lastActivityLeg is a mgdleg or logmgdleg: \n");
					fprintf(logFile, "ErrorID = 1, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
					fprintf(logFile, "\n");
				}//end if
			}//end else
		}//end if
		
		else if(strcmp(radLastLegPtr->rowtype,"mtcnote") == 0 || strcmp(radLastLegPtr->rowtype,"ownersign") == 0)
		{
			if(!preCrewPtr->on1 && !preCrewPtr->on2 && verbose){
				fprintf(logFile, "\n Found crew with lastActivityLeg = 'mtcnote' or 'ownersign' but no (ON1,OFF1) and (ON2,OFF2) information: \n");
				fprintf(logFile, "ErrorID = 0, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
				fprintf(logFile, "\n");
			}//end if			//the last leg is a maintenance or an ownersigning
			preCrewPtr->availAirportID = radLastLegPtr->inaptid;
			if((radLastLegPtr->rec_outtime <= dt_pwStart) && 
				 (radLastLegPtr->rec_intime >= dt_pwStart) &&
				 (radLastLegPtr->rec_intime <= dt_addToDateTime(Minutes, optParam.maintTmForReassign, dt_pwStart)))
			{ //MX case #1A
				on2Info = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1));
				off2Info = (preCrewPtr->on2) ? (preCrewPtr->off2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.maxDutyTm, preCrewPtr->off1));
				if(((radLastLegPtr->rec_intime >= preCrewPtr->on1 && radLastLegPtr->rec_intime <= preCrewPtr->off1) || 
				   (radLastLegPtr->rec_intime >= preCrewPtr->on2 && radLastLegPtr->rec_intime <= preCrewPtr->off2)) &&
				   (radLastLegPtr->rec_intime <= dt_addToDateTime(Hours, 24*(int)preCrewPtr->stayLate, preCrewPtr->tourEndTm)))
				{ 
					//that is, if ((mx.end in (on1, off1) || mx.end in (on2, off2)) AND (mx.end < tourEndTm+stayLate))
					//pilot will be on duty at the end of MX 
					preCrewPtr->cscd_canStartLater = 0;

					tPreFltTm = (preCrewPtr->firstFlightOfTour) ?
								((preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart) ? optParam.preFlightTm : optParam.firstPreFltTm) :
								(optParam.firstPreFltTm);
			    
					if(radLastLegPtr->rec_intime >= preCrewPtr->on1 && radLastLegPtr->rec_intime <= preCrewPtr->off1)
					{ 
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -tPreFltTm, radLastLegPtr->rec_intime), preCrewPtr->on1);
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
					}
					if(radLastLegPtr->rec_intime >= preCrewPtr->on2 && radLastLegPtr->rec_intime <= preCrewPtr->off2)
					{ 
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -tPreFltTm, radLastLegPtr->rec_intime), preCrewPtr->on2);
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on2, &days, &hours, &minutes, &seconds, &msecs);
					}
					dutytime = (24 * 60 * days) + (60 * hours) + minutes;
					preCrewPtr->dutyTime=dutytime;

			        if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else 
						preCrewPtr->blockTime=0;				
				}
				else if((radLastLegPtr->rec_intime >= preCrewPtr->off1) && (radLastLegPtr->rec_intime <= on2Info) &&
						(radLastLegPtr->rec_intime <= dt_addToDateTime(Hours, 24*(int)preCrewPtr->stayLate, preCrewPtr->tourEndTm)))
				{
					//pilot will be resting at the end of MX, but end of MX is between off1 and on2
					preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1));
					//Depending on pwS, we might or might not need to consider run start time
					if(dt_pwStart <= preCrewPtr->off1){
						//Don't need to consider run start time
						preCrewPtr->cscd_canStartLater = 1;
					}//end if
					else {
						//Do need to consider run start time
						preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1) ? 1 : 0;
					}//end else
					preCrewPtr->blockTime=0;
					preCrewPtr->dutyTime=0;
				}//end else
				else if((radLastLegPtr->rec_intime >= off2Info)&&
						(radLastLegPtr->rec_intime < dt_addToDateTime(Hours, 24*(int)preCrewPtr->stayLate, preCrewPtr->tourEndTm)))
				{
					//pilot will be resting at the end of MX, but end of MX is bigger than off2
					//This is not suppose to happen (or at least, very rarely)
					//Here we do NOT need to consider run start time - always have enough time to notify
					preCrewPtr->cscd_canStartLater = 1;
					preCrewPtr->cscd_availDT = dt_addToDateTime(Minutes, optParam.minRestTm, off2Info);
                    preCrewPtr->blockTime=0;
					preCrewPtr->dutyTime=0;
				}//end elseif
				else 
					preCrewPtr->availDuringPlanningWindow=0;
			}//end if MX case #1A

			else if((radLastLegPtr->rec_outtime <= dt_pwStart) && 
				    (radLastLegPtr->rec_intime >= dt_pwStart) &&
				    (radLastLegPtr->rec_intime > dt_addToDateTime(Minutes, optParam.maintTmForReassign, dt_pwStart)))
                { //MX case #1B 
				if((preCrewPtr->on1 <= dt_pwStart) &&
				   (preCrewPtr->off1 >= dt_pwStart))
				{
					//pilot will be on duty at the start of planning window
					preCrewPtr->cscd_canStartLater = 0;

					tPreFltTm = (preCrewPtr->firstFlightOfTour) ?
								((preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart) ? optParam.preFlightTm : optParam.firstPreFltTm) :
								(optParam.firstPreFltTm);

					//preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
					//							   dt_addToDateTime(Minutes, -tPreFltTm, dt_pwStart));
//RLZ replaced the above 06202008
					preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
										 dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));
		
						                       
				    dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			        dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				    preCrewPtr->dutyTime=dutytime;

			        if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else 
						preCrewPtr->blockTime=0;
				}//end if
				else 
				{
					//pilot will be resting at the start of planning window
					preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.minTimeToNotify, preCrewPtr->off1));
					//Do need to consider run start time to determine canStartLater	
					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1) ? 1 : 0;
					preCrewPtr->dutyTime=0;
			        if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else 
						preCrewPtr->blockTime=0;
				}//end else
			}//end elseif MX case #1B
			else if(radLastLegPtr->rec_intime < dt_pwStart)
			{ //MX case #2
				tPreFltTm = (preCrewPtr->firstFlightOfTour) ?
							((preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart) ? optParam.preFlightTm : optParam.firstPreFltTm) :
							(optParam.firstPreFltTm);

				if((preCrewPtr->on1 <= dt_pwStart)&&(preCrewPtr->off1 >= dt_pwStart))
				{
					//pilot will be on duty at the start of planning window
					//preCrewPtr->cscd_canStartLater = 0;
				//	preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
				//								   dt_addToDateTime(Minutes, -tPreFltTm, dt_pwStart)); 

					preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, 
						dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));


					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) < 
									dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on1)) ? 1 : 0;
				    dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			        dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				    preCrewPtr->dutyTime=dutytime;
					if(preCrewPtr->on1 < radLastLegPtr->rec_outtime)
						preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
					else
						preCrewPtr->blockTime=0;						   
				}//end if
				else {
					//pilot will be resting at the start of planning window
					//Do need to consider run start time
					if(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1){
						preCrewPtr->cscd_canStartLater = 1;
						preCrewPtr->cscd_availDT = dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1);
						preCrewPtr->dutyTime=0;
						preCrewPtr->blockTime=0;
					}//end if
					else{
						preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.minTimeToNotify, preCrewPtr->off1));
						preCrewPtr->cscd_canStartLater = (preCrewPtr->on2) ? 0 : 1;
						preCrewPtr->dutyTime=0;
						preCrewPtr->blockTime=0;
					}//end else																						
				}//end else
			}//end elseif MX case #2
			else 
			{
				//new MX case - Display Warning
				if(verbose)
				{
					fprintf(logFile, "\n Found error(s) for crew whose lastActivityLeg is a 'mtcnote' or 'ownersign': \n");
					fprintf(logFile, "ErrorID = 2, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
					fprintf(logFile, "\n");
				}//end if
			}//end else
		}//end else if
		
		else {
			//rowtype is other than logmgdleg, mgdleg, mcnote, ownersign - Display Warning
			if(verbose){
				fprintf(logFile, "\n Found error for crew whose lastActivityLeg is not one of these: 'mgdleg', 'logmgdleg', 'mtcnote', 'ownersign' \n");
				fprintf(logFile, "ErrorID = 3, CrewID = %s, lastActivityLeg.rowtype = %s, lastActivityLeg.recid = %d", preCrewPtr->employeenumber, radLastLegPtr->rowtype, radLastLegPtr->recid);
				fprintf(logFile, "\n");
			}//end if
			return(0);
		}
		
		//lastly: check if pilot is available during the planning window
		if (dt_addToDateTime(Minutes, optParam.finalPostFltTm, preCrewPtr->cscd_availDT) > dt_addToDateTime(Hours, 24.*preCrewPtr->stayLate, preCrewPtr->tourEndTm)){
			preCrewPtr->availDuringPlanningWindow = 0;
			return(0);
		}
	}//end if radLastLegPtr
	else if (!radLastLegPtr&&!cstrlPtr ) 
	{
		//there is no lastLeg information from lastActivityLeg
		if(preCrewPtr->firstFlightOfTour && preCrewPtr->firstFlightOfTour->rec_outtime < dt_pwStart )
		{
			//lastActivityLeg is not identified but firstFlightOfTour is defined - Error, Display Warning
			if(verbose)
			{
				fprintf(logFile, "\n Found crew with lastActivityLeg but no firstFlightOfTour: \n");
				fprintf(logFile, "ErrorID = 4, CrewID = %s, lastActivityLeg is not identified but firstFlightOfTour is defined", preCrewPtr->employeenumber);
				fprintf(logFile, "\n");
			}//end if
		}//end if 
		else{
			//this is the 'tour start' case
			preCrewPtr->availAirportID = preCrewPtr->startLoc;
			updateLoc = 1;
			if((preCrewPtr->on1) && (dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm) <= dt_pwStart)){  
				//pilot has started tour at the start of planning window and on1 information is available
				if(preCrewPtr->on1 <= dt_pwStart && dt_pwStart <= preCrewPtr->off1){
					//pilot will be on duty at the start of planning window
					preCrewPtr->cscd_canStartLater = 0;
					preCrewPtr->cscd_availDT = Max(preCrewPtr->on1, Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime),
						                           dt_addToDateTime(Minutes, -optParam.firstPreFltTm, dt_pwStart)));
					dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			        dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				    preCrewPtr->dutyTime=dutytime;
					preCrewPtr->blockTime=0;
				    
				}//end if
				else {
					//pilot will be resting at the start of planning window
					//Do need to consider run start time
					if(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <= preCrewPtr->off1){
						preCrewPtr->cscd_canStartLater = 1;
						preCrewPtr->cscd_availDT = dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1);
					}//end if
					else{
						preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm + optParam.minTimeToNotify, preCrewPtr->off1));
						preCrewPtr->cscd_canStartLater = (preCrewPtr->on2) ? 0 : 1;
					}//end else																											
					preCrewPtr->dutyTime=0;
					preCrewPtr->blockTime=0;
				}//end else
			}//end if
			else if((dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm) <= dt_pwStart)){  
				//pilot has started tour at the start of planning window but on1 information is not available
				preCrewPtr->cscd_canStartLater = 1;
				preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, optParam.minTmToTourStart, dt_runTime),
				                           Max(dt_addToDateTime(Minutes, -optParam.firstPreFltTm, dt_pwStart),
										       dt_addToDateTime(Minutes, -24*60*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm)));
				preCrewPtr->dutyTime=0;
				preCrewPtr->blockTime=0;				    				
			}//end elseif
			else {
				//pilot has not started tour at the start of planning window (therefore on1 is not available)
				preCrewPtr->cscd_canStartLater = 1;
				if(preCrewPtr->on2)
				{
					preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm),
											       Min(dt_addToDateTime(Minutes, optParam.minTmToTourStart, dt_runTime),
												       preCrewPtr->on2));
				}//end if
				else 
				{
					preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm),
												   dt_addToDateTime(Minutes, optParam.minTmToTourStart, dt_runTime));
				}//end else
				preCrewPtr->dutyTime=0;//need to change, later
				preCrewPtr->blockTime=0;
			}//end else
		}//end else
	}//end else radLastLegPtr  
	//else((radLastLegPtr && cstrlPtr && radLastLegPtr->rec_intime < cstrl_arrTm)||(!radLastLegPtr && cstrlPtr))
	//leg followed by a Travel leg or only the travel leg
	else if ((radLastLegPtr && cstrlPtr && radLastLegPtr->rec_intime < cstrl_arrTm &&(strcmp(radLastLegPtr->rowtype,"logmgdleg") == 0 || strcmp(radLastLegPtr->rowtype,"mgdleg") == 0))||(!radLastLegPtr && cstrlPtr)){
	       preCrewPtr->availAirportID = cstrlPtr->arr_aptID;
			if((cstrl_dptTm < dt_pwStart) && (cstrl_arrTm >= dt_pwStart)){
				//commercial flight start time < planning window start < flight end time, i.e crew is on commercial flight at the planning window start
				preCrewPtr->cscd_availDT = cstrl_arrTm;
				preCrewPtr->cscd_canStartLater = 0;
				if(cstrl_arrTm <= preCrewPtr->off1 && cstrl_dptTm >=preCrewPtr->on1)
				  dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
				else if(cstrl_arrTm <= preCrewPtr->off2 && (cstrl_dptTm >= preCrewPtr->on2)) 
				  dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on2, &days, &hours, &minutes, &seconds, &msecs);
				else 
				  dt_dateTimeDiff(preCrewPtr->cscd_availDT, cstrl_dptTm, &days, &hours, &minutes, &seconds, &msecs);
				dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				preCrewPtr->dutyTime=dutytime;
				preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
			}//end if
			else if((cstrl_arrTm < dt_pwStart) && (cstrl_dptTm >= dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm))){
				//commerical flight in time < planning window start 
				if((preCrewPtr->on1 < dt_pwStart) && (dt_pwStart <= preCrewPtr->off1)){
					//pilot will be on duty at the start of planning window
					if(cstrl_arrTm > preCrewPtr->on1){
						//last commercial flight leg and planning window start are in the same duty period
						preCrewPtr->cscd_availDT = Max(cstrl_arrTm, 
													   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime));
						//preCrewPtr->cscd_availDT = Max(cstrl_arrTm, 
						//	Max(dt_addToDateTime(Minutes, -((preCrewPtr->hasFlown)? optParam.preFlightTm:optParam.firstPreFltTm), dt_pwStart), 
						//								   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime)));
						preCrewPtr->cscd_canStartLater = 0;
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			            dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				        preCrewPtr->dutyTime=dutytime;
                        preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);

					}//end if
					else {
						//last commercial flight leg and planning window start are in different duty periods - depends on run start time
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -((preCrewPtr->hasFlown)? optParam.preFlightTm:optParam.firstPreFltTm), dt_pwStart), 
														Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime), 
															preCrewPtr->on1));
						preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) < 
														  dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on1)) ? 1 : 0;
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
						dutytime = (24 * 60 * days) + (60 * hours) + minutes;
						preCrewPtr->dutyTime=dutytime;
						preCrewPtr->blockTime=0;
					}//end else
				}//end if
				else {
					//pilot will be resting at the start of planning window
					preCrewPtr->cscd_availDT = (preCrewPtr->on2) ? (preCrewPtr->on2) : (dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1? preCrewPtr->off1:cstrl_arrTm));
					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <  (preCrewPtr->off1? preCrewPtr->off1:(preCrewPtr->on2?dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on2):cstrl_arrTm)))? 1 : 0;
					preCrewPtr->blockTime=0;
					preCrewPtr->dutyTime=0;
					/*preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -((preCrewPtr->hasFlown)? optParam.preFlightTm:optParam.firstPreFltTm), dt_pwStart), 
														Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime), 
															cstrl_arrTm));
					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <  cstrl_dptTm) ? 1 : 0;
					preCrewPtr->blockTime=0;
					if((cstrl_arrTm <= preCrewPtr->off1) && cstrl_dptTm >=preCrewPtr->on1)
				       dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
				    else if((cstrl_arrTm <= preCrewPtr->off2) && (cstrl_dptTm >= preCrewPtr->on2)) 
				       dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on2, &days, &hours, &minutes, &seconds, &msecs);
				    else 
				       dt_dateTimeDiff(preCrewPtr->cscd_availDT, cstrl_dptTm, &days, &hours, &minutes, &seconds, &msecs);
					dt_dateTimeDiff(preCrewPtr->cscd_availDT, cstrl_dptTm, &days, &hours, &minutes, &seconds, &msecs);
				    dutytime = (24 * 60 * days) + (60 * hours) + minutes;
					preCrewPtr->dutyTime = dutytime;*/
				}//end else
			}//end elseif
			else {
				//commerical flight departes after pws, but arrival time is still within one day of run time
				preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime), cstrl_arrTm);
					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <  cstrl_dptTm) ? 1 : 0;
					preCrewPtr->blockTime=0;
					if((cstrl_arrTm <= preCrewPtr->off1) && cstrl_dptTm >=preCrewPtr->on1)
				       dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
				    else if((cstrl_arrTm <= preCrewPtr->off2) && (cstrl_dptTm >= preCrewPtr->on2)) 
				       dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on2, &days, &hours, &minutes, &seconds, &msecs);
				    else 
				       dt_dateTimeDiff(preCrewPtr->cscd_availDT, cstrl_dptTm, &days, &hours, &minutes, &seconds, &msecs);
				    dutytime = (24 * 60 * days) + (60 * hours) + minutes;
					preCrewPtr->dutyTime = dutytime;
				//if(verbose){
				//	fprintf(logFile, "\n Found error(s) for crew whose lastleg is commercial leg: \n");
				//	fprintf(logFile, "ErrorID = 1, employnumber = %s, lastcommericalleg.dpt_aptID = %d, lastcommericalleg.arr_aptID = %d", preCrewPtr->employeenumber, cstrlPtr->dpt_aptID, cstrlPtr->arr_aptID);
				//	fprintf(logFile, "\n");
				//}//end if
			}//end else 
	}
	//MX, take the travel regardless of the time. exclude other cases indentified above.
	else
	{       preCrewPtr->availAirportID = cstrlPtr->arr_aptID;
	        if((cstrl_dptTm < dt_pwStart) && (cstrl_arrTm >= dt_pwStart)){
				//commercial flight start time < planning window start < flight end time, i.e crew is on commercial flight at the planning window start
				preCrewPtr->cscd_availDT = cstrl_arrTm;
				preCrewPtr->cscd_canStartLater = 0;
				if(cstrl_arrTm <= preCrewPtr->off1 && cstrl_dptTm >=preCrewPtr->on1)
				  dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
				else if(cstrl_arrTm <= preCrewPtr->off2 && (cstrl_dptTm >= preCrewPtr->on2)) 
				  dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on2, &days, &hours, &minutes, &seconds, &msecs);
				else 
				  dt_dateTimeDiff(preCrewPtr->cscd_availDT, cstrl_dptTm, &days, &hours, &minutes, &seconds, &msecs);
				dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				preCrewPtr->dutyTime=dutytime;
				preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);
			}//end if
	        else if((cstrl_arrTm < dt_pwStart) && (cstrl_dptTm >= dt_addToDateTime(Hours, -24*(int)preCrewPtr->startEarly, preCrewPtr->tourStartTm))){
				//commerical flight in time < planning window start 
				if((preCrewPtr->on1 < dt_pwStart) && (dt_pwStart <= preCrewPtr->off1)){
					//pilot will be on duty at the start of planning window
					if(cstrl_arrTm > preCrewPtr->on1){
						//last commercial flight leg and planning window start are in the same duty period
						preCrewPtr->cscd_availDT = Max(cstrl_arrTm, 
							Max(dt_addToDateTime(Minutes, -((preCrewPtr->hasFlown)? optParam.preFlightTm:optParam.firstPreFltTm), dt_pwStart), 
														   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime)));
						preCrewPtr->cscd_canStartLater = 0;
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
			            dutytime = (24 * 60 * days) + (60 * hours) + minutes;
				        preCrewPtr->dutyTime=dutytime;
                        preCrewPtr->blockTime=getCrewBlockTime(preCrewPtr);

					}//end if
					else {
						//last commercial flight leg and planning window start are in different duty periods - depends on run start time
						preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, -((preCrewPtr->hasFlown)? optParam.preFlightTm:optParam.firstPreFltTm), dt_pwStart), 
														Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime), 
															preCrewPtr->on1));
						preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) < 
														  dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on1)) ? 1 : 0;
						dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
						dutytime = (24 * 60 * days) + (60 * hours) + minutes;
						preCrewPtr->dutyTime=dutytime;
						preCrewPtr->blockTime=0;
					}//end else
				}//end if
				else if((preCrewPtr->off1 && dt_pwStart > preCrewPtr->off1)||(!preCrewPtr->off1 && preCrewPtr->on2)){
					//first duty has pasted by pws
					   preCrewPtr->cscd_availDT = Max(preCrewPtr->on2 ? (preCrewPtr->on2) : dt_addToDateTime(Minutes, optParam.minRestTm, Max(preCrewPtr->off1,cstrl_arrTm)),dt_addToDateTime(Minutes, -((preCrewPtr->hasFlown)? optParam.preFlightTm:optParam.firstPreFltTm), dt_pwStart));
					   preCrewPtr->cscd_canStartLater = dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <  (preCrewPtr->off1? preCrewPtr->off1:dt_addToDateTime(Minutes, -optParam.minRestTm, preCrewPtr->on2))? 1 : 0;
					   preCrewPtr->blockTime=0;
					   dt_dateTimeDiff(preCrewPtr->cscd_availDT, (preCrewPtr->on2 ? (preCrewPtr->on2) : dt_addToDateTime(Minutes, optParam.minRestTm, preCrewPtr->off1)), &days, &hours, &minutes, &seconds, &msecs);
					   dutytime = (24 * 60 * days) + (60 * hours) + minutes;
					   preCrewPtr->dutyTime = dutytime;
				}//end else
				else{
                  //on1, off1, on2, off2 are all null
					   dt_dateTimeDiff(dt_pwStart, cstrl_dptTm, &days, &hours, &minutes, &seconds, &msecs);
                       dutytime = (24 * 60 * days) + (60 * hours) + minutes;
					   if(dutytime > optParam.crewDutyAfterCommFlt*60){
                          preCrewPtr->cscd_availDT = dt_addToDateTime(Minutes, optParam.minRestTm, cstrl_arrTm);
						  preCrewPtr->cscd_canStartLater = 1;
                          preCrewPtr->dutyTime = 0;
                          preCrewPtr->blockTime= 0;
					   }
					   else{
					      preCrewPtr->cscd_availDT = Max(cstrl_arrTm, 
							Max(dt_addToDateTime(Minutes, -((preCrewPtr->hasFlown)? optParam.preFlightTm:optParam.firstPreFltTm), dt_pwStart), 
														   dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime)));
                            preCrewPtr->cscd_canStartLater = 0;
                            dt_dateTimeDiff(preCrewPtr->cscd_availDT, cstrl_dptTm, &days, &hours, &minutes, &seconds, &msecs);
                            dutytime = (24 * 60 * days) + (60 * hours) + minutes;
							preCrewPtr->dutyTime =dutytime;
                            preCrewPtr->blockTime= 0;
					   }
				}
			}//end elseif
			else {
				//commerical flight departes after pws, but arrival time is still within one day of run time
				preCrewPtr->cscd_availDT = Max(dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime), cstrl_arrTm);
					preCrewPtr->cscd_canStartLater = (dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_runTime) <  cstrl_dptTm) ? 1 : 0;
					preCrewPtr->blockTime=0;
					if((cstrl_arrTm <= preCrewPtr->off1) && cstrl_dptTm >=preCrewPtr->on1)
				       dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on1, &days, &hours, &minutes, &seconds, &msecs);
				    else if((cstrl_arrTm <= preCrewPtr->off2) && (cstrl_dptTm >= preCrewPtr->on2)) 
				       dt_dateTimeDiff(preCrewPtr->cscd_availDT, preCrewPtr->on2, &days, &hours, &minutes, &seconds, &msecs);
				    else 
				       dt_dateTimeDiff(preCrewPtr->cscd_availDT, cstrl_dptTm, &days, &hours, &minutes, &seconds, &msecs);
				    dutytime = (24 * 60 * days) + (60 * hours) + minutes;
					preCrewPtr->dutyTime = dutytime;
				//if(verbose){
				//	fprintf(logFile, "\n Found error(s) for crew whose lastleg is commercial leg: \n");
				//	fprintf(logFile, "ErrorID = 1, employnumber = %s, lastcommericalleg.dpt_aptID = %d, lastcommericalleg.arr_aptID = %d", preCrewPtr->employeenumber, cstrlPtr->dpt_aptID, cstrlPtr->arr_aptID);
				//	fprintf(logFile, "\n");
				//}//end if
			}//end else 
	}
	
	
	if ( preCrewPtr->on2 && optParam.estimateCrewLoc && updateLoc && preCrewPtr->assignedACID && preCrewPtr->AC_AirportID_beforePW && dt_addToDateTime(Hours, -12 - 24.*preCrewPtr->startEarly, preCrewPtr->tourStartTm) < dt_pwStart) 
		//This crew has crewassignment in BW, we update its location same as the AC. RLZ 01/15/2008
         preCrewPtr->availAirportID = preCrewPtr->AC_AirportID_beforePW;
	if(! preCrewPtr->availAirportID)
		preCrewPtr->availAirportID = preCrewPtr->startLoc;

	return(0);
}//end of function


/********************************************************************************
 *	Function   readMacInfo - MAC			     Date last modified: 08/29/08 ANG
 *	Purpose:  Read in M-aircraft-related info from mysql db.
 ********************************************************************************/
typedef enum {
	acid, ctid, mac_end_of_list = 255
} macSqlColumns;
static int
readMacInfo(MY_CONNECTION *myconn)
{
	extern char *macInfoSQL;

	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	MYSQL_ROW row;
	my_ulonglong rowCount, rows;
	int a;
	
	if(!myDoQuery(myconn, macInfoSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"readMacInfo(): 0 rows returned.\n");
		return(0);
	}

	if(!(macInfoList = (MacInfo *) calloc((size_t) rowCount + 1, sizeof(MacInfo)))) {
		logMsg(logFile,"%s Line %d, Out of Memory in readMacInfo().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		if(! row[acid]) 
			continue;
		macInfoList[numMacInfo].aircraftID = atoi(row[acid]);
		macInfoList[numMacInfo].contractID = atoi(row[ctid]);
		numMacInfo++;
	}

	if(verbose) {
		fprintf (logFile, "\n M-Aircraft (Mac): \n");
		fprintf(logFile,"+------------+------------+\n");
		fprintf(logFile,"| aircraftID | contractID |\n");
		fprintf(logFile,"+------------+------------+\n");
		for(a = 0; a < numMacInfo; a++) {
			fprintf(logFile,"| %10d | %10d |\n", macInfoList[a].aircraftID, macInfoList[a].contractID);
		}
		fprintf(logFile,"+------------+------------+\n");
		fflush(logFile);
	}

	mysql_free_result(res);
	return(0);
}
/*******************************************************************************************************
*	Function  readMacInfoFromOracleDB() - MAC - 09/03/08 ANG
*	Purpose:  read Mac related info directly from Oracle
*******************************************************************************************************/
typedef enum {
	acid2 = 0, ctid2
} macOracleColumns;

static int
readMacInfoFromOracleDB(ORACLE_SOCKET *orl_socket)
{
    extern char *macInfoOracleSQL; 
	extern int numMacInfo;
	int a;
	int rowCount = 0;

	if(Orlconnection_doquery(orl_socket, macInfoOracleSQL))
	{  logMsg(logFile,"%s Line %d: failed to execute query /*macInfoOracleSQL*/ and get data\n", __FILE__,__LINE__);
	   exit(1);
	}  

	while (Orlconnection_fetch(orl_socket)==0){
		rowCount = OrlConnection_num_rows(orl_socket);
	}

	if(!(macInfoList = (MacInfo *) calloc((size_t) rowCount + 1, sizeof(MacInfo)))) {
		logMsg(logFile,"%s Line %d, Out of Memory in readMacInfo().\n",__FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if(Orlconnection_doquery(orl_socket, macInfoOracleSQL))
	{  logMsg(logFile,"%s Line %d: failed to execute query /*macInfoOracleSQL*/ and get data\n", __FILE__,__LINE__);
	   exit(1);
	}  

	while(Orlconnection_fetch(orl_socket)==0) {
		macInfoList[numMacInfo].aircraftID = atoi(orl_socket->results[acid2]);
		macInfoList[numMacInfo].contractID = atoi(orl_socket->results[ctid2]);
		numMacInfo++;
	}

	if(verbose) {
		fprintf (logFile, "\n M-Aircraft (Mac): \n");
		fprintf(logFile,"+------------+------------+\n");
		fprintf(logFile,"| aircraftID | contractID |\n");
		fprintf(logFile,"+------------+------------+\n");
		for(a = 0; a < numMacInfo; a++) {
			fprintf(logFile,"| %10d | %10d |\n", macInfoList[a].aircraftID, macInfoList[a].contractID);
		}
		fprintf(logFile,"+------------+------------+\n");
		fflush(logFile);
	}

	return 0;
}

/*******************************************************************************************************
*	Function  writeToIncrementalRevenueTable - 03/30/09 ANG
*	Purpose:  write incremental revenue used in this run to a table in MySQL database
*******************************************************************************************************/
void writeToIncrementalRevenueTable(MY_CONNECTION *myconn, int scenarioid)
{
	int i, j, count;
	QITEM *qi;
	DemandInfo *diPtr;
	Demand *dmdPtr, dmdBuf;
	KeyWord kw;
	KW_Rec *kwRecPtr; 
	BINTREENODE *tmp, *tmp1, *tmp2;
	Ratios *rPtr, rBuf;

	char sqlBuf[102400], sqlBuf1[102400];
	char incRevBuf[128], tempBuf[128], kwBuf[512];
	char tbuf[32];
	extern char *username;
	extern int verbose; 
	//extern BINTREENODE *dmdKeyWdRoot;
	//extern BINTREENODE *dmdXDemandIdRoot;

	if(! myconn) {
		logMsg(logFile,"%s Line %d: no MySQL connection!\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(! scenarioid) {
		logMsg(logFile,"%s Line %d: no scenarioid!\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if(! username || ! strlen(username)) {
		username = getenv("USERNAME");
		if(! username)
			username = "user_name?";
	}

	for(tmp = Minimum(dmdKeyWdRoot); tmp; tmp = Successor(tmp)) {
		diPtr = (DemandInfo *) getTreeObject(tmp);
		dmdBuf.demandID = diPtr->demandid;
		
		tmp2 = TreeSearch(dmdXDemandIdRoot,&dmdBuf,demandIdCompare);
		if(! tmp2) {
			// didn't find this demandid.
			// not in planning window
			continue;
		}
		dmdPtr = (Demand *) getTreeObject(tmp2);

		strcpy(incRevBuf, " ");

		for(i = 1; i < MAX_AC_TYPES; i++) {
		//for(i = 0; i < (MAX_AC_TYPES - 1); i++) {
			if(dmdPtr->incRevStatus[i] == -1){
				sprintf(tempBuf, "0, ");
				strcat(incRevBuf, tempBuf);
				//fprintf(logFile,"|  N/A  ");
			}
			else {
				sprintf(tempBuf, "%4.2f, ", diPtr->incRev[i]);
				strcat(incRevBuf, tempBuf);
				//fprintf(logFile,"| %5.0f ", diPtr->incRev[i]);
			}
		}
		// do sovereign here
		//if(dmdPtr->incRevStatus[i] == -1){
		//	sprintf(tempBuf, "0, ");
		//	strcat(incRevBuf, tempBuf);
		//	//fprintf(logFile,"|    N/A ");
		//}
		//else{
		//	sprintf(tempBuf, "%4.2f, ", diPtr->incRev[i]);
		//	strcat(incRevBuf, tempBuf);
		//	//fprintf(logFile,"| %6.0f ", diPtr->incRev[i]);
		//}

		//if(diPtr->smallestAllowableIssue)
		//	sprintf(tbuf,"*%d", diPtr->smallestAllowableIssue);
		//else
		//	strcpy(tbuf,"  ");
		//fprintf(logFile,"| %10s%2s ", getsp_text(diPtr->smallestAllowableSeqPos), tbuf);

		//strcpy(tbuf2, getsp_text(diPtr->contract_seqpos));
		//strcpy(tbuf3, getsp_text(diPtr->seqpos));

		//fprintf(logFile,"| %7d | %7d | %16s | %16s | %7d | %7d | %-8s | %3d | %3d | %-10s | %3d | %3d | %-10s | ",
		//	diPtr->ownerid,
		//	diPtr->contractid,
		//	(diPtr->dmd_outtime) ? dt_DateTimeToDateTimeString(diPtr->dmd_outtime,tbuf,"%Y/%m/%d %H:%M") : "",
		//	(diPtr->outtime) ? dt_DateTimeToDateTimeString(diPtr->outtime,tbuf1,"%Y/%m/%d %H:%M") : "",
		//	diPtr->demandid,
		//	diPtr->otherid,
		//	diPtr->shortname,
		//	diPtr->contract_seqpos,
		//	diPtr->contract_actypeid,
		//	tbuf2,
		//	diPtr->seqpos,
		//	diPtr->actypeid,
		//	tbuf3);

		strcpy(kwBuf, "'");
		count = QGetCount(diPtr->kwq);
		for(i = 0, qi = QGetHead(diPtr->kwq); i < count; ++i, qi = QGetNext(diPtr->kwq, qi)) {
			kwRecPtr =  (KW_Rec *)QGetObject(qi);
			sprintf(tempBuf, "%s, ", getKW_text(kwRecPtr->kw));
			strcat(kwBuf, tempBuf);
			//fprintf(logFile,"%s, ", getKW_text(kwRecPtr->kw));
		}
		sprintf(tempBuf, "'");
		strcat(kwBuf, tempBuf);

		sprintf(sqlBuf, "insert into optimizer_increvenue values( now(), '%s', '%s', %d, %d, %d, ", 
				dt_DateTimeToDateTimeString(dt_time_tToDateTime(optParam.windowStart), tbuf, "%Y/%m/%d %H:%M"),
				username,
				scenarioid,
				diPtr->demandid,
				diPtr->contract_actypeid);
		strcat(sqlBuf, incRevBuf);
		sprintf(sqlBuf1, "%d, %s)", 		
			(diPtr->smallestAllowableIssue > 0 ? diPtr->smallestAllowableIssue : 0),
				kwBuf);
		strcat(sqlBuf, sqlBuf1);

		//fprintf(logFile, "%s\n", sqlBuf);
		if(!(myDoWrite(myconn,sqlBuf))) {
			logMsg(logFile,"WARNING: Error found when writing to incremental revenue table \n %s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
			//logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
			//writeWarningData(myconn); exit(1);
		}
	}

	if(verbose)
		logMsg(logFile,"** Incremental revenue calculated in this run is stored into optimizer_increvenue table unless warning message(s) printed above.\n");

}