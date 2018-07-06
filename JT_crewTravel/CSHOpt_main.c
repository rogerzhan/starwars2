char *svnKeyWords[] = {
"$LastChangedDate: 2009-04-03 14:28:16 -0400 (Fri, 03 Apr 2009) $",
"$Revision$",
"$LastChangedBy: lzhan $",
"$HeadURL: http://csintranetdev01/repos/trunk/optimizer/cmdWin/CSHOpt_mainDir/CSHOpt_main.c $",
"$Id: CSHOpt_main.c 18249 2009-04-03 18:28:16Z lzhan $",
"optimParams.txt at revision 3232 for snapshotOptimParams.txt and 3230 for optimParams.txt",
(char *) 0
};
// 2007/08/22 -- OAG modifications

#include "os_config.h"
#include "datetime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <direct.h>
#include <io.h>
#include <errno.h>
#include "runway.h"
#include "srchAndRpl.h"
#include "logMsg.h"
#include "params.h"
#include "my_mysql.h"
#include "airportLatLon.h"
#include "CSHOpt_readInput.h"
#include "CSHOpt_processInput.h"
#include "CSHOpt_pairCrews.h"
#include "CSHOpt_dutyNodes.h"
#include "CSHOpt_arcs.h"
#include "CSHOpt_struct.h"
#include "CSHOpt_scheduleSolver.h"
#include "CSHOpt_output.h"
#include "myoracle.h"
//SO CHANGE
#include "CSHOpt_buildOagOD.h"
//#include "CSHOpt_oag.h"
//END SO CHANGE

int numOAGCallsEarly=0;
int numOAGCallsLate=0;
//int numOAGCalls=0;

//summary log - 12/23/08 ANG
//FILE *summaryFile;

// logging
FILE *logFile;
char logFileName[512];
time_t tp;
char *username = NULL;

// other values
Moisture wetDry; // see runway.h. this is set with parameter type_3_exclusions
time_t run_time_t; // run time in GMT as time_t
DateTime dt_run_time_GMT; // run time in dt_ format in GMT
DateTime dt_run_time_LOC; // run time in dt_ format in local time zone
int locFromGMTinMinutes;  // number of minutes between gmt and local time
int oracleDirect = 0;

// SQL statements
extern char *statSQL = (char *) 0; // 02/12/08 ANG
extern char *statSchedSQL = (char *) 0; // 11/10/08 ANG
extern char *statSchedSQL2 = (char *) 0; // 11/10/08 ANG
extern char *statSchedSQL3 = (char *) 0; // 11/10/08 ANG
extern char *macStatSQL = (char *) 0; // 11/17/08 ANG
extern char *macStatSQL2 = (char *) 0; // 11/17/08 ANG
extern char *statSQLD1 = (char *) 0; // 12/05/08 ANG
extern char *statSQLD2 = (char *) 0; // 12/05/08 ANG
extern char *statSQLD3 = (char *) 0; // 12/05/08 ANG
extern char *statSchedSQLD1 = (char *) 0; // 12/08/08 ANG
extern char *statSchedSQLD2 = (char *) 0; // 12/08/08 ANG
extern char *statSchedSQLD3 = (char *) 0; // 12/08/08 ANG
extern char *statSchedSQL2D1 = (char *) 0; // 12/08/08 ANG
extern char *statSchedSQL2D2 = (char *) 0; // 12/08/08 ANG
extern char *statSchedSQL2D3 = (char *) 0; // 12/08/08 ANG
extern char *statSchedSQL3D1 = (char *) 0; // 12/08/08 ANG
extern char *statSchedSQL3D2 = (char *) 0; // 12/08/08 ANG
extern char *statSchedSQL3D3 = (char *) 0; // 12/08/08 ANG
extern char *macStatSQLD1 = (char *) 0; // 12/08/08 ANG
extern char *macStatSQLD2 = (char *) 0; // 12/08/08 ANG
extern char *macStatSQLD3 = (char *) 0; // 12/08/08 ANG
extern char *macStatSQL2D1 = (char *) 0; // 12/08/08 ANG
extern char *macStatSQL2D2 = (char *) 0; // 12/08/08 ANG
extern char *macStatSQL2D3 = (char *) 0; // 12/08/08 ANG

char *demandSQL = (char *) 0;
char *legSQL = (char *) 0;
char *fuelLegSQL = (char *) 0;
char *whereAreThePlanesSQL = (char *) 0;
char *mtcnoteSQL = (char *) 0;
char *airportSQL = (char *) 0;
char *exclusionSQL = (char *) 0;
char *curfewexclusionSQL = (char *) 0;
char *crewIdSQL = (char *) 0;
char *bwCrewPairDataSQL = (char *) 0;   //Roger's fix for hasFlownFirst.
char *bwCrewDataSQL = (char *) 0;
char *csCrewDataSQL = (char *) 0;
char *ssCrewDataSQL = (char *) 0;
char *charterStatsSQL = (char *) 0;
char *pairConstraintsSQL = (char *) 0;
char *intnlCertSQL = (char *) 0;
char *demandnumbyzoneSQL = (char *) 0;
char *cstravelSQL = (char *) 0;
char *cstravelOracleSQL = (char *) 0;
char *timezoneOracleSQL = (char *) 0;
char *peakdayscontractratesOracleSQL = (char *) 0;
char *csCrewDataOracleSQL = (char *) 0;
char *ssCrewDataOracleSQL = (char *) 0;
char *ratioOracleSQL = (char *) 0;
char *upgradeDowngradeOracleSQL = (char *) 0;
char *macInfoSQL = (char *) 0; //MAC - 09/02/08 ANG
char *macInfoOracleSQL = (char *) 0; //MAC - 09/02/08 ANG

// database login
char *pdataPath = (char *) 0;
char *mysqlServer = (char *) 0;
char *mysqlUser = (char *) 0;
char *mysqlPW = (char *) 0;
char *mysqlServer_oag = (char *) 0;
char *mysqlUser_oag = (char *) 0;
char *mysqlPW_oag = (char *) 0;
char *remoteMysqlServer = (char *) 0;
char *remoteMysqlUser = (char *) 0;
char *remoteMysqlPW = (char *) 0;
char *database = (char *) 0;
char *database_oag = (char *) 0;
char *remotedatabase = (char *) 0;
char *oracleServer = (char *) 0;
char *oracleUser = (char *) 0;
char *oraclePW = (char *) 0;
char *oracleDB = (char *) 0;
char *oracleServerSS = (char *) 0;
char *oracleUserSS = (char *) 0;
char *oraclePWSS = (char *) 0;
char *oracleDBSS = (char *) 0;

int local_scenarioid;
int remote_scenarioid;
//QLIST *errorinfoList;
Warning_error_Entry *errorinfoList=NULL;
ORACLE_CONFIG *config;
ORACLE_SOCKET *orl_socket;
int errorNumber;
int withOag;
extern MY_CONNECTION *myconn;
extern MY_CONNECTION *remotemyconn;
extern MY_CONNECTION *myconn_oag;
extern OptParameters optParam;
extern int numPlaneArcs;
extern int numDutyArcs;
extern int numDutyArcCopies;
extern int numPickupArcs;
extern int numCrewPlaneArcs;
extern int numCrewPickupArcs;
extern int numArcsToFirstDuties;
extern int numDutiesByType[10];
static char windowStartStr[32];
static char windowEndStr[32];

extern void writeODTable();

//TEMP FOR BYPASS
extern int numOptCrewPairs; 
extern int numCrewPairs;

//////////////////////////////////////////////////////////////////////////////////////
// The following variables can be set from the command line in addition to the parameter
// file. The command line value overrides the parameter file value.
///////////////////////////////////////////////////////////////////////////////////////
typedef struct cmdlinevar {
	char *varName;
	char *varValue;
} CmdLineVar;

CmdLineVar commandLineVars[] = {
	{ "windowStart", (char *) 0 },
	{ "windowEnd", (char *) 0 },   // not really a command line var, but it is convenient to place value here
	{ "planningWindowDuration", (char *) 0 },
	{ "pdataPath", (char *) 0 },
	{ "logDir", (char *) 0 },
	{ "fakeRuntime", (char *) 0 },
	{ "updateRemoteServer", (char *) 0 },
	{ "changeNxtDayPenalty", (char *) 0 },
	{ "changeTodayPenalty", (char *) 0 },
	{ "maxUpgrades", (char *) 0 },
	{ "downgrPairPriority1", (char *) 0 }, //ignore pairing request priority 1 - TEMP FIX - 01/21/09 ANG
	{ "pairingLevel", (char *) 0 },
	{ "oracleDirect", (char *) 0 },
	{ "runType", (char *) 0 },
	{ "withOag", (char *) 0 },
	{ "withCTC", (char *) 0 },
	{ "withMac", (char *) 0 },
	{ "earlierAvailability", (char *) 0 },  // to get earlier availDT for crew - 10/30/07 ANG
	{ "autoFlyHome", (char *) 0 },
	{ "inclContingencyDmd", (char *) 0 }, //to include contingency demands - 09/08/08 ANG
	{ "inclInfeasExgSol", (char *) 0 }, //to include infeasible cases in existing solutions - 05/22/08 ANG
	{ "includeLockedUmlg", (char *) 0 }, //to include all locked-chartered demands - 02/12/08 ANG
	{ "ignoreSwapLoc", (char *) 0 }, //to include all locked-chartered demands - 02/12/08 ANG
	{ "ignoreMacOS", (char *) 0 }, //to ignore all green blocks scheduled on Mac - MAC - 10/23/08 ANG
	{ "ignoreMacOSMX", (char *) 0 }, //to ignore both maintenance AND green blocks on Mac that started AFTER windowStart - MAC - 01/07/08 ANG
	{ "ignoreMac", (char *) 0 }, //to ignore all green and mx blocks scheduled on Mac - MAC - 01/05/09 ANG
	{ "ignoreMacDmd", (char *) 0 }, //to ignore all green and mx blocks scheduled on Mac - MAC - 01/05/09 ANG
	{ "ignoreAllOS", (char *) 0 }, //to ignore all green blocks scheduled on any aircraft - 10/28/08 ANG
	{ "runWithContingency", (char *) 0 }, // run with contingency option
	{ "runOptStats", (char *) 0 }, //run optimizer statistics at the end of optimizer run - 02/12/08 ANG
	{ "runOptStatsByDay", (char *) 0 }, //run optimizer statistics at the end of optimizer run, separated by day - 12/05/08 ANG
    { "uncovFlyHomePenalty", (char *) 0 },
    { "uncovFlyHomePenalty2", (char *) 0 }, // 04/22/08 ANG
    { "turnTime", (char *) 0 },
    { "maintTurnTime", (char *) 0 },
    { "preFlightTm", (char *) 0 },
    { "postFlightTm", (char *) 0 },
    { "firstPreFltTm", (char *) 0 },
    { "finalPostFltTm", (char *) 0 },
    { "preBoardTime", (char *) 0 },
    { "peakDayLevel_1_Adj", (char *) 0 },
    { "vectorWin", (char *) 0 },
	{ "uncovDemoPenalty", (char *) 0 },
    { "postArrivalTime", (char *) 0 },
	{ "estimateCrewLoc", (char *) 0 },      // estimate Crew Location from bitwise 
	{ "dutyStartFromMidnight", (char *) 0 },
    { "minTimeToNotify", (char *) 0 },
	{ "minTmToTourStart", (char *) 0 },
	{ "planningWindowStart", (char *) 0 },
	{ "planningfakeRuntime", (char *) 0 },
	{ "recoveryDemandIDs", (char *) 0 },
	{ "downgradeRecovery", (char *) 0 },
	{ "upgradeRecovery", (char *) 0 },
	{ "recoveryAdj_early", (char *) 0 },
	{ "recoveryAdj_late", (char *) 0 },
	{ "exgCPACLock", (char *) 0 },
	{ "exgCPACBonus", (char *) 0 }, //02/19/09 ANG
	{ "maxCrewExtension", (char *) 0 },
	{ "prohibitStealingPlanes", (char *) 0 },	
	{ "crewDutyAfterCommFlt", (char *) 0 },
	{ "macBonus", (char *) 0 }, //MAC - 09/05/08 ANG
    { "updateforSimu", (char *) 0 },
	{ "macPenalty", (char *) 0 },
	{ "travelcutoff", (char *) 0 }, //09/11/09 ANG
	{ "sendhomecutoff", (char *) 0 }, //04/18/09 Jintao
	//{ "writeSimulationData", (char *) 0 }, //03/11/09 ANG
	{ (char *) 0, (char *) 0 }
};
//////////////////////////////////////////////////////////////////////////////////////
// The above variables can be set from the command line or from the parameter file.
///////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////
// The following variables should be in the parameter file. For these values we don't
// expect the user to want to override the parameter file value from the command line.
///////////////////////////////////////////////////////////////////////////////////////
int verbose = 0;
int verbose1 = 1;  //Turn off some lengthy log... RLZ
//////////////////////////////////////////////////////////////////////////////////////
// The above variables should be in the parameter file
///////////////////////////////////////////////////////////////////////////////////////


static char *umsg[] = {
	"usage: CSHOpt_main --params=parameterFile",
	"    where parameterFile is the file of optimizer input parameter values.",
	"",
	"The following command line options will override parameter values of the",
	"same name in the parameter file:",
	"",
	"\"--windowStart=YYYY/MM/DD HH:MI\"",
	"    where:",
	"    YYYY is the 4 digit year.",
	"    MM is the month, 01 to 12.",
	"    DD is the day of the month, 01 to 31.",
	"    HH is the hour, 00 to 23.",
	"    MI is the minute, 00 to 59.",
	"",
	"--pdataPath=path",
	"    where 'path' is the path name of a file containing database login information.",
	"",
	"--logDir=dirName",
	"    where 'dirName' is the name of the directory in which to create the log file.",
	"",
	"--planningWindowDuration=days",
	"    where days is a number from 1 to 3.",
	"",
	"--maxUpgrades=N",
	"    where N is an integer indicating the maximum number of upgrades.",
	"",
	"--pairingLevel=N",
	"    where N is an integer indicating the pairing level.",
	"",
	"--changeTodayPenalty=N.N",
	"    where N.N is a double precision number indicating the value of the changeTodayPenalty parameter.",
	"",
	"--changeNxtDayPenalty=N.N",
	"    where N.N is a double precision number indicating the value of the changeNxtDayPenalty parameter.",
	"",
	"\"--fakeRuntime=YYYY/MM/DD HH:MI\"",
	"    where option value has the same format as windowStart.",
	"",
	"--inputOnly=Y",
	"    where 'inputOnly=Y' indicates program should stop after reading input data",
	"",
	"--oracleDirect=Y",
	"    where 'oracleDirect=Y' indicates that queries that can be run directly against Oracle should be run that way.",
	"",
	"--updateRemoteServer=Y",
	"    where 'updateRemoteServer=Y' indicates MySQL tables on the remote server should be updated",
	"    if the program runs to completion.",
	"",
	"--user=username",
	"    where username is the value to be written to the username column in the optimizer_results table.",
	"",
	"Examples:",
	"    CSHOpt_main --params=optimParams.txt \"--windowStart=2006/03/20 11:00\" --planningWindowDuration=2",
	"    CSHOpt_main --params=optimParams.txt --inputOnly=Y --planningWindowDuration=2",
	"    CSHOpt_main --params=optimParams.txt --pdataPath=loginInfo.txt \"--windowStart=2006/03/20 11:00\"",
	"    CSHOpt_main --params=optimParams.txt \"--windowStart=2006/03/20 11:00\" \"--fakeRuntime=2006/03/19 19:00\"",
	(char *) 0
};

static void usage(char *msg);
static void setCmdLineVar(char *varName, char *varValue);
char *getCmdLineVar(char *varName);
static char *makeLogFileName(void);
static void getParameters(char *params);
static void getStartWin(DateTime inputDateDT, time_t *windowStart, time_t *windowEnd);
int populateAcSchedLegIndList(void);

static void
usage(char *msg)
{
	int i = 0;

	fprintf(stderr,"%s\n", msg);
	while(umsg[i]) {
		fprintf(stderr,"%s\n", umsg[i]);
		++i;
	}
	writeWarningData(myconn); exit(1);
}

int
main(int argc, char **argv)
{
	char *params = (char *) 0;
	DateTime inputDateDT;
	int errNbr;
	int inputOnly = 0;
	int updateRemoteServer = 0;
	int days, hours, minutes, seconds, msecs;
	int adjustlocFromGMTinMinutes = 0;

	char *arg;
	char *paramVal;
	//char writetodbstring1[200];
    errorNumber=0;
	
	//debug
	//Exclusion *expptr;
	//int *aptListPtr;
	//end debug

	/////////////////////////////////////////////////////////
	// this is not needed any more:
	// initialize array of pointers to AirportLatLon table //
	// initializeAirportLatLonPtrArrayByAptID();
	/////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////////////////////////////
	// get parameter file and options that will override parameter file variables //////
	while(--argc) 
	{
		arg = *(++argv);
		if(strncasecmp("--windowStart=", arg, 14) == 0) 
			setCmdLineVar("windowStart", arg + 14);
		else if(strncasecmp("--pdataPath=", arg, 12) == 0) 
			setCmdLineVar("pdataPath", arg + 12);
		else if(strncasecmp("--changeNxtDayPenalty=", arg, 22) == 0) 
			setCmdLineVar("changeNxtDayPenalty", arg + 22);
		else if(strncasecmp("--changeTodayPenalty=", arg, 21) == 0) 
			setCmdLineVar("changeTodayPenalty", arg + 21);
		else if(strncasecmp("--maxUpgrades=", arg, 14) == 0) 
			setCmdLineVar("maxUpgrades", arg + 14);
		else if(strncasecmp("--pairingLevel=", arg, 15) == 0) 
			setCmdLineVar("pairingLevel", arg + 15);
		else if(strncasecmp("--planningWindowDuration=", arg, 25) == 0) 
			setCmdLineVar("planningWindowDuration", arg + 25);
		else if(strncasecmp("--fakeRuntime=", arg, 14) == 0) 
			setCmdLineVar("fakeRuntime", arg + 14);
		else if(strncasecmp("--inputOnly=Y", arg, 13) == 0) 
			inputOnly = 1;
		else if(strncasecmp("--user=", arg, 7) == 0) 
			username = strdup(arg + 7);
		else if(strncasecmp("--updateRemoteServer=", arg, 21) == 0)
			setCmdLineVar("updateRemoteServer", arg + 21);
		else if(strncasecmp("--logDir=", arg, 9) == 0) 
			setCmdLineVar("logDir", arg + 9);
		else if(strncasecmp("--params=", arg, 9) == 0)
			params = arg + 9;
		else if(strncasecmp("--adjLocFromGMT=", arg, 16) == 0)
			adjustlocFromGMTinMinutes = atoi(arg + 16);
		else if(strncasecmp("--oracleDirect=", arg, 15) == 0)
			setCmdLineVar("oracleDirect", arg + 15);
		else if(strncasecmp("--estimateCrewLoc=", arg, 18) == 0)
			setCmdLineVar("estimateCrewLoc", arg + 18);
		else if(strncasecmp("--runType=", arg, 10) == 0)
			setCmdLineVar("runType", arg + 10);
        else if(strncasecmp("--withOag=", arg, 10) == 0)
			setCmdLineVar("withOag", arg + 10);
		else if(strncasecmp("--withCTC=", arg, 10) == 0)
		    setCmdLineVar("withCTC", arg + 10);
		else if(strncasecmp("--withMac=", arg, 10) == 0)
		    setCmdLineVar("withMac", arg + 10);
		else if(strncasecmp("--earlierAvailability=", arg, 22) == 0)
			setCmdLineVar("earlierAvailability", arg + 22);
		else if(strncasecmp("--downgrPairPriority1=", arg, 22) == 0) //01/21/09 ANG
			setCmdLineVar("downgrPairPriority1", arg + 22);
		else if(strncasecmp("--autoFlyHome=", arg, 14) == 0)
			setCmdLineVar("autoFlyHome", arg + 14);
		else if(strncasecmp("--inclContingencyDmd=", arg, 21) == 0) //09/08/08 - ANG
			setCmdLineVar("inclContingencyDmd", arg + 21);  
        else if(strncasecmp("--inclInfeasExgSol=", arg, 19) == 0) //05/22/08 - ANG
			setCmdLineVar("inclInfeasExgSol", arg + 19); //05/22/08 - ANG  
        else if(strncasecmp("--includeLockedUmlg=", arg, 20) == 0) //02/12/08 - ANG
			setCmdLineVar("includeLockedUmlg", arg + 20); //02/12/08 - ANG  
		else if(strncasecmp("--ignoreSwapLoc=", arg, 16) == 0) 
			setCmdLineVar("ignoreSwapLoc", arg + 16); 
		else if(strncasecmp("--ignoreMacOS=", arg, 14) == 0) //MAC - 10/22/08 ANG
			setCmdLineVar("ignoreMacOS", arg + 14); 
		else if(strncasecmp("--ignoreMacOSMX=", arg, 16) == 0) //MAC - 01/07/09 ANG
			setCmdLineVar("ignoreMacOSMX", arg + 16); 
		else if(strncasecmp("--ignoreMac=", arg, 12) == 0) //MAC - 01/05/09 ANG
			setCmdLineVar("ignoreMac", arg + 12); 
		else if(strncasecmp("--ignoreMacDmd=", arg, 15) == 0) //MAC - 01/06/09 ANG
			setCmdLineVar("ignoreMacDmd", arg + 15); 
		else if(strncasecmp("--ignoreAllOS=", arg, 14) == 0) //10/28/08 ANG
			setCmdLineVar("ignoreAllOS", arg + 14); 
		else if(strncasecmp("--runWithContingency=", arg, 21) == 0) //05/13/08 - Jintao
			setCmdLineVar("runWithContingency", arg + 21); //05/13/08 - Jintao
        else if(strncasecmp("--runOptStats=", arg, 14) == 0) //02/12/08 - ANG
			setCmdLineVar("runOptStats", arg + 14); //02/12/08 - ANG
        else if(strncasecmp("--runOptStatsByDay=", arg, 19) == 0) //12/05/08 - ANG
			setCmdLineVar("runOptStatsByDay", arg + 19); //12/05/08 - ANG
        else if(strncasecmp("--uncovFlyHomePenalty=", arg, 22) == 0)
			setCmdLineVar("uncovFlyHomePenalty", arg + 22);
        else if(strncasecmp("--uncovFlyHomePenalty2=", arg, 23) == 0) // 04/22/08 ANG
			setCmdLineVar("uncovFlyHomePenalty2", arg + 23); 
        else if(strncasecmp("--travelcutoff=", arg, 15) == 0)//09/11/08 ANG
			setCmdLineVar("travelcutoff", arg + 15);
        else if(strncasecmp("--turnTime=", arg, 11) == 0)
			setCmdLineVar("turnTime", arg + 11);
        else if(strncasecmp("--maintTurnTime=", arg, 16) == 0)
			setCmdLineVar("maintTurnTime", arg + 16);
        else if(strncasecmp("--preFlightTm=", arg, 14) == 0)
			setCmdLineVar("preFlightTm", arg + 14);
        else if(strncasecmp("--postFlightTm=", arg, 15) == 0)
			setCmdLineVar("postFlightTm", arg + 15);
		else if(strncasecmp("--firstPreFltTm=", arg, 16) == 0)
			setCmdLineVar("firstPreFltTm", arg + 16);
        else if(strncasecmp("--finalPostFltTm=", arg, 17) == 0)
			setCmdLineVar("finalPostFltTm", arg + 17);
        else if(strncasecmp("--preBoardTime=", arg, 15) == 0)
			setCmdLineVar("preBoardTime", arg + 15);
        else if(strncasecmp("--peakDayLevel_1_Adj=", arg, 21) == 0)
			setCmdLineVar("peakDayLevel_1_Adj", arg + 21);
        else if(strncasecmp("--vectorWin=", arg, 12) == 0)
			setCmdLineVar("vectorWin", arg + 12);
        else if(strncasecmp("--postArrivalTime=", arg, 18) == 0)
			setCmdLineVar("postArrivalTime", arg + 18);
		else if(strncasecmp("--uncovDemoPenalty=", arg, 19) == 0)
			setCmdLineVar("uncovDemoPenalty", arg + 19);
        else if(strncasecmp("--dutyStartFromMidnight=", arg, 24) == 0)
			setCmdLineVar("dutyStartFromMidnight", arg + 24);
        else if(strncasecmp("--minTimeToDuty=", arg, 16) == 0)
			setCmdLineVar("minTimeToDuty", arg + 16);
        else if(strncasecmp("--minTimeToNotify=", arg, 18) == 0)
			setCmdLineVar("minTimeToNotify", arg + 18);
		else if(strncasecmp("--minTmToTourStart=", arg, 19) == 0)
			setCmdLineVar("minTmToTourStart", arg + 19);
		else if(strncasecmp("--planningWindowStart=", arg, 22) == 0)
			setCmdLineVar("planningWindowStart", arg + 22);   //Number of minutes from current time		
		else if(strncasecmp("--planningFakeRuntime=", arg, 22) == 0) // 02/19/08 ANG
			setCmdLineVar("planningFakeRuntime", arg + 22);   //Number of minutes from current time - 02/19/08 ANG
		else if(strncasecmp("--recoveryDemandIDs=", arg, 20) == 0)
			setCmdLineVar("recoveryDemandIDs", arg + 20); 
		else if(strncasecmp("--downgradeRecovery=", arg, 20) == 0)
    		setCmdLineVar("downgradeRecovery", arg + 20); 
		else if(strncasecmp("--upgradeRecovery=", arg, 18) == 0)
    		setCmdLineVar("upgradeRecovery", arg + 18); 
		else if(strncasecmp("--recoveryAdj_early=", arg, 20) == 0)
    		setCmdLineVar("recoveryAdj_early", arg + 20); 
		else if(strncasecmp("--recoveryAdj_late=", arg, 19) == 0)
    		setCmdLineVar("recoveryAdj_late", arg + 19); 
		else if(strncasecmp("--exgCPACLock=", arg, 14) == 0) 
    		setCmdLineVar("exgCPACLock", arg + 14); 
		else if(strncasecmp("--exgCPACBonus=", arg, 15) == 0)//02/19/09 ANG
    		setCmdLineVar("exgCPACBonus", arg + 15); 
		else if(strncasecmp("--maxCrewExtension=", arg, 19) == 0)
    		setCmdLineVar("maxCrewExtension", arg + 19); 
		else if(strncasecmp("--prohibitStealingPlanes=", arg, 25) == 0)
    		setCmdLineVar("prohibitStealingPlanes", arg + 25); 
        else if(strncasecmp("--crewDutyAfterCommFlt=", arg, 23) == 0)
    		setCmdLineVar("crewDutyAfterCommFlt", arg + 23); 
        else if(strncasecmp("--macPenalty=", arg, 13) == 0) //MAC - 09/05/08 ANG
    		setCmdLineVar("macPenalty", arg + 13); 
        else if(strncasecmp("--macBonus=", arg, 11) == 0)
    		setCmdLineVar("macBonus", arg + 11); 
		else if(strncasecmp("--updateforSimu=", arg, 16) == 0) //Simulation
			setCmdLineVar("updateforSimu", arg + 16);
        else if(strncasecmp("--sendhomecutoff=", arg, 17) == 0) //send home paramter
			setCmdLineVar("sendhomecutoff", arg + 17);
		else {
			fprintf(stderr,"\"%s\" is not a recognized command argument.\n", arg);
			exit(1);
		}
	}
	////////////////////////////////////////////////////////////////////////////////////

//	/////////////////////////////////////////////// MOVED BELOW - START - 02/19/08 ANG
//	// get and save run time //////////////////////
//	if(!(paramVal = getCmdLineVar("fakeRuntime")))
//		run_time_t = time(NULL);
//	else
//		run_time_t = DateTimeToTime_t(dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", paramVal, NULL, &errNbr));
//	dt_run_time_GMT = dt_time_tToDateTime(run_time_t);
//	dt_run_time_LOC = dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n", asctime(localtime(&run_time_t)), NULL, &errNbr);
//
//	dt_dateTimeDiff(dt_run_time_GMT, dt_run_time_LOC, &days, &hours, &minutes, &seconds, &msecs);
//	locFromGMTinMinutes = (24 * 60 * days) + (60 * hours) + minutes;
//	locFromGMTinMinutes += adjustlocFromGMTinMinutes;
//	/////////////////////////////////////////////// MOVED BELOW - END - 02/19/08 ANG

	// load in the parameters
	if(!params)
		usage("Specify a parameter file.");
	if(! loadParams(params)) {
		fprintf(stderr,"Can't get parameters from parameter file %s\n", params);
		exit(1);
	}

	if(! (paramVal = getCmdLineVar("oracleDirect"))) {
		/* required: "oracleDirect" */
		if(!(paramVal = getParamValue("oracleDirect"))) {
			fprintf(stderr,"%s Line %d, Required parameter \"oracleDirect\" missing from parameter file.\n",
				__FILE__,__LINE__);
			exit(1);
		}
		//if(strncasecmp(paramVal,"Y",1) == 0)
		//	oracleDirect = 1;
		oracleDirect = atoi(paramVal);
	}
	else {
		//if(strncasecmp(paramVal,"Y",1) == 0)
		//	oracleDirect = 1;
		oracleDirect = atoi(paramVal);
	}
	
	if(! (paramVal = getCmdLineVar("paramsOracle"))) {
		/* required: "paramsOracle" */
		if(!(paramVal = getParamValue("paramsOracle"))) {
			fprintf(stderr,"%s Line %d, Required parameter \"paramsOracle\" missing from parameter file.\n",
				__FILE__,__LINE__);
			exit(1);
		}
		optParam.paramsOracle = atoi(paramVal);
	}
	else {
		optParam.paramsOracle = atoi(paramVal);
	}
	
	
	if(oracleDirect == 1) {
		int ret;
		char cmdbuf[512];
		strcpy(cmdbuf,"c:/cygwin/bin/bash --login -i ");
		if(!(paramVal = getParamValue("oracleScript"))) {
			fprintf(stderr,"%s Line %d, Required parameter \"oracleScript\" missing from parameter file.\n", __FILE__,__LINE__);
			exit(1);
		}
		strcat(cmdbuf,paramVal);
		ret = system(cmdbuf);
		if(ret != 0) {
			fprintf(stderr,"%s Line %d, script %s failed.\n", __FILE__,__LINE__, paramVal);
			exit(1);
		}
	}


	if(! (paramVal = getCmdLineVar("planningWindowDuration"))) {
		/* required: "planningWindowDuration" */
		if(!(paramVal = getParamValue("planningWindowDuration"))) {
			fprintf(stderr,"%s Line %d, Required parameter \"planningWindowDuration\" missing from parameter file.\n",
				__FILE__,__LINE__);
			exit(1);
		}
		optParam.planningWindowDuration = atoi(paramVal);
	}
	else {
		optParam.planningWindowDuration = atoi(paramVal);
	}
	if(optParam.planningWindowDuration < 1 || optParam.planningWindowDuration > MAX_WINDOW_DURATION) {
		fprintf(stderr,"%s Line %d, planningWindowDuration value %d out of range.\n", __FILE__,__LINE__,optParam.planningWindowDuration);
		exit(1);
	}

	/* required: "updateRemoteServer" */
	if(! (paramVal = getCmdLineVar("updateRemoteServer"))) {
		/* required: "updateRemoteServer" */
		if(!(paramVal = getParamValue("updateRemoteServer"))) {
			fprintf(stderr,"%s Line %d, Required parameter \"updateRemoteServer\" missing from parameter file.\n", __FILE__,__LINE__);
			exit(1);
		}
		if(strcasecmp(paramVal, "Y", 1) == 0)
			updateRemoteServer = 1;
	}
	else {
		if(strcasecmp(paramVal, "Y", 1) == 0)
			updateRemoteServer = 1;
	}

	/* required: "planningWindowStart" */

	if(! (paramVal = getCmdLineVar("planningWindowStart"))) {
		if(!(paramVal = getParamValue("planningWindowStart"))) {
			fprintf(stderr,"Required parameter \"planningWindowStart\" missing from parameter file.\n");
			exit(1);
		}
	}

	optParam.planningWindowStart = atoi(paramVal);

	/* required: "planningFakeRuntime" - 02/19/08 ANG */

	if(! (paramVal = getCmdLineVar("planningFakeRuntime"))) {
		if(!(paramVal = getParamValue("planningFakeRuntime"))) {
			fprintf(stderr,"Required parameter \"planningFakeRuntime\" missing from parameter file.\n");
			exit(1);
		}
	}

	optParam.planningFakeRuntime = atoi(paramVal);

	/////////////////////////////////////////////// MOVED HERE - START - 02/19/08 ANG
	// get and save run time //////////////////////
	if(!(paramVal = getCmdLineVar("fakeRuntime")))
		run_time_t = time(NULL)+ (optParam.planningFakeRuntime * 60);
	else
		run_time_t = DateTimeToTime_t(dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", paramVal, NULL, &errNbr));
	dt_run_time_GMT = dt_time_tToDateTime(run_time_t);
	dt_run_time_LOC = dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n", asctime(localtime(&run_time_t)), NULL, &errNbr);

	dt_dateTimeDiff(dt_run_time_GMT, dt_run_time_LOC, &days, &hours, &minutes, &seconds, &msecs);
	locFromGMTinMinutes = (24 * 60 * days) + (60 * hours) + minutes;
	locFromGMTinMinutes += adjustlocFromGMTinMinutes;
	////////////////////////////////////////////// MOVED HERE - END - 02/19/08 ANG

	/* required: "dayEndTime" */
	if(!(paramVal = getParamValue("dayEndTime"))) {
		fprintf(stderr,"Required parameter \"dayEndTime\" missing from parameter file.\n");
		exit(1);
	}
	optParam.dayEndTime = atoi(paramVal);

	if(!(paramVal = getCmdLineVar("windowStart"))) {
		getStartWin((DateTime) 0, &optParam.windowStart, &optParam.windowEnd);
	}
	else {
		inputDateDT = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", paramVal, NULL, &errNbr);
		if(inputDateDT == BadDate) {
			fprintf(stderr,"Enter a valid date in \"YYYY/MM/DD HH:MI\" format.\n");
			exit(1);
		}
		getStartWin(inputDateDT, &optParam.windowStart, &optParam.windowEnd);
	}
	(void) dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
		asctime(gmtime(&(optParam.windowStart))), NULL, &errNbr),windowStartStr,"%Y/%m/%d %H:%M");
	setCmdLineVar("windowStart", windowStartStr);
	(void) dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
		asctime(gmtime(&(optParam.windowEnd))), NULL, &errNbr),windowEndStr,"%Y/%m/%d %H:%M");
	setCmdLineVar("windowEnd", windowEndStr);

	if(! (paramVal = getCmdLineVar("runType"))) {
		/* required: "runType" */
		if(!(paramVal = getParamValue("runType"))) 
		{
			fprintf(stderr,"%s Line %d, Required parameter \"runType\" missing from parameter file.\n",
				__FILE__,__LINE__);
			exit(1);
		}
		optParam.runType = atoi(paramVal);
	}
	else {
		optParam.runType = atoi(paramVal);
	}

	//START - 02/12/08 ANG
	if(! (paramVal = getCmdLineVar("includeLockedUmlg"))) {
		/* required: "includeLockedUmlg" */
		if(!(paramVal = getParamValue("includeLockedUmlg"))) 
		{
			fprintf(stderr,"%s Line %d, Required parameter \"includeLockedUmlg\" missing from parameter file.\n",
				__FILE__,__LINE__);
			exit(1);
		}
		optParam.includeLockedUmlg = atoi(paramVal);
	}
	else {
		optParam.includeLockedUmlg = atoi(paramVal);
	}
	//END - 02/12/08 ANG

	//START - 09/08/08 ANG
	if(! (paramVal = getCmdLineVar("inclContingencyDmd"))) {
		/* required: "inclContingencyDmd" */
		if(!(paramVal = getParamValue("inclContingencyDmd"))) 
		{
			fprintf(stderr,"%s Line %d, Required parameter \"inclContingencyDmd\" missing from parameter file.\n",
				__FILE__,__LINE__);
			exit(1);
		}
		if(atoi(paramVal) == 1)
			optParam.inclContingencyDmd = 1;
		else
			optParam.inclContingencyDmd = 0;
	}
	else {
		if(atoi(paramVal) == 1)
			optParam.inclContingencyDmd = 1;
		else
			optParam.inclContingencyDmd = 0;
	}
	//END - 09/08/08 ANG

	if(! (paramVal = getCmdLineVar("withOag"))) {
		/* required: "runType" */
		if(!(paramVal = getParamValue("withOag"))) 
		{
			fprintf(stderr,"%s Line %d, Required parameter \"withOag\" missing from parameter file.\n",
				__FILE__,__LINE__);
			exit(1);
		}
		if(strcasecmp(paramVal, "Y", 1) == 0)
			optParam.withOag = 1;
		else
			optParam.withOag = 0;
	}
	else {
		if(strcasecmp(paramVal, "Y", 1) == 0)
			optParam.withOag = 1;
		else
			optParam.withOag = 0;
	}
    if(optParam.runType > 0)
		optParam.withOag=1;
    /*//**********************************
	//test for dev
	if(optParam.withOag==1)
	{   int ret;
		ret = system("C:\\optimizerBK\\refreshdata.bat");
		if(ret != 0) {
			fprintf(stderr,"%s Line %d, refresh data failed.\n", __FILE__,__LINE__);
			exit(1);
		}
	}
	//test for dev
	//**********************************/
	withOag=optParam.withOag;
	/////////////////////////////
	// Get some of the parameters
	/////////////////////////////
	getParameters(params);
	//Connect to MySQL db
	myconn = myDoConnect(mysqlServer, mysqlUser, mysqlPW ? mysqlPW : "", database, 3306, (char *) 0, CLIENT_FOUND_ROWS);
	if(myconn->my_errno) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__, myconn->my_errno, myconn->my_error_msg);
		exit(1);
	}

	//Connect to MySQL OAG db
	myconn_oag = myDoConnect(mysqlServer_oag, mysqlUser_oag, mysqlPW_oag ? mysqlPW_oag : "", database_oag, 3306, (char *) 0, CLIENT_FOUND_ROWS);
	if(myconn->my_errno) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__, myconn->my_errno, myconn->my_error_msg);
		exit(1);
	}

	if(updateRemoteServer) {
		//Connect to remote MySQL db
		remotemyconn = myDoConnect(remoteMysqlServer, remoteMysqlUser, remoteMysqlPW ? remoteMysqlPW : "", remotedatabase, 3306, (char *) 0, CLIENT_FOUND_ROWS);
		if(remotemyconn->my_errno) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__, remotemyconn->my_errno, remotemyconn->my_error_msg);
			exit(1);
		}
	}

	if(oracleDirect == 2 || optParam.paramsOracle ==1){
       orl_socket = Orlconnection_init_conn(oracleServer, oracleUser, oraclePW, oracleDB);
       if(orl_socket == NULL) {
		  logMsg(logFile,"%s Line %d: db connection error\n", __FILE__,__LINE__);
		  exit(1);
	   }
	   if (optParam.paramsOracle==1) loadParams2(orl_socket, logFile);
	}



	//SO CHANGE
	//if(optParam.runType == 0) //if an Optimization run (not an OAG pre-process or update)
//END SO CHANGE
	initializeOutputData(updateRemoteServer);

	if(updateRemoteServer) {
		mysql_close(remotemyconn->mysock);
		remotemyconn = NULL;
	}

	readInputData(mysqlServer, mysqlUser, mysqlPW, database);
	//debug
//	logMsg(logFile,"** Debug:\n");
//	if(getExclusion(Excln_Airport_CurfewStart, 3453, 0, &expptr))
//		fprintf(logFile,"curfew start for airportID 3453: %d\n", expptr->secondID);
//	else
//		fprintf(logFile,"curfew start for airportID 3453 not found.\n");
//	if(getExclusion(Excln_Airport_CurfewEnd, 3453, 0, &expptr))
//		fprintf(logFile,"curfew end for airportID 3453: %d\n", expptr->secondID);
//	else
//		fprintf(logFile,"curfew start for airportID 3453 not found.\n");
//	if(getExclusion(Excln_AircraftType_Airport, 11, 3453, &expptr))
//		fprintf(logFile,"aircraftTypeID 11 excluded from airportID 3453.\n");
//	else
//		fprintf(logFile,"aircraftTypeID 11 not excluded from airportID 3453.\n");
//	if(getAircraftTypeAirportExclusions(11, &aptListPtr)) {
//		fprintf(logFile,"airports excluding aircraftTypeID 11:\n");
//		while(*aptListPtr) {
//			fprintf(logFile,"%d ", *(aptListPtr));
//			++aptListPtr;
//		}
//		fprintf(logFile,"\n");
//	}
//	else
//		fprintf(logFile,"no airports excluding aircraftTypeID 11:\n");
//	logMsg(logFile,"** End Debug:\n");
	//end debug
	if(inputOnly)
		exit(0);
	
	fprintf(logFile, "\n");
	logMsg(logFile,"** Start processInputData\n\n");
    processInputData();
	populateAcSchedLegIndList(); // 03/14/08 ANG

//SO CHANGE
	if(withOag==1){
		logMsg(logFile,"** Start buildOagODTable\n\n");
		buildOagODTable();
	}
	if(optParam.runType == 0){ //if an Optimization run (not an OAG pre-process or update){
//END SO CHANGE
		logMsg(logFile,"** Start pairCrews\n\n");
		pairCrews();
		//fprintf(logFile,"%d calls made to OAG function during crew pairing.\n\n", numOAGCalls);
		fprintf(logFile,"%d + %d calls made to OAG function during crew pairing.\n\n", numOAGCallsEarly, numOAGCallsLate);

		//move buildExistingTours() up here to capture shorter turnTime - 05/09/08 ANG
		logMsg (logFile, "** Going to build existing tours\n");
		buildExistingTours();
		printAcList(); // 05/28/09 ANG
		logMsg (logFile, "** Built existing tours\n\n");

		logMsg(logFile,"** Start createDutyNodes\n\n");
		createDutyNodes();
		fprintf(logFile, "%d duty nodes created.\n\n", numDutiesByType[9]);
		logMsg(logFile,"** Start createArcs\n\n");
		createArcs();
		logMsg(logFile,"** End createArcs\n\n");
		logMsg(logFile,"** Start optimization\n\n");
		getOptimalSchedule();
		logMsg(logFile,"** End optimization\n\n");

		writeOutputData(myconn, local_scenarioid);
		writeWarningData(myconn);
		if(withOag==1) writeODTable();  
		if(updateRemoteServer) {
			//Connect to remote MySQL db
			remotemyconn = myDoConnect(remoteMysqlServer, remoteMysqlUser, remoteMysqlPW ? remoteMysqlPW : "", remotedatabase, 3306, (char *) 0, CLIENT_FOUND_ROWS);
			if(remotemyconn->my_errno) {
				logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__, remotemyconn->my_errno, remotemyconn->my_error_msg);
				writeWarningData(myconn); exit(1);
			}
			writeOutputData(remotemyconn, remote_scenarioid);
			writeWarningData(remotemyconn);
		}
//SO CHANGE
	}
//END SO CHANGE
	else
	{//writeWarningData(myconn); //Do not write the warning message for a good OAG data update
	 updateResultsTableforOAG(myconn, local_scenarioid);
	 if(updateRemoteServer) {
		//Connect to remote MySQL db
		remotemyconn = myDoConnect(remoteMysqlServer, remoteMysqlUser, remoteMysqlPW ? remoteMysqlPW : "", remotedatabase, 3306, (char *) 0, CLIENT_FOUND_ROWS);
		if(remotemyconn->my_errno) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__, remotemyconn->my_errno, remotemyconn->my_error_msg);
			writeWarningData(myconn); exit(1);
		}
		//writeWarningData(remotemyconn); //Do not write the warning message for a good OAG data update
		updateResultsTableforOAG(remotemyconn, remote_scenarioid);
	 }
	}
	mysql_close(myconn->mysock);
	myconn = NULL;
	if(updateRemoteServer) {
		mysql_close(remotemyconn->mysock);
		remotemyconn = NULL;
	}
	logMsg(logFile,"** The whole process was successful.**\n\n");
    exit(0);
}


static void
setCmdLineVar(char *varName, char *varValue)
{
	CmdLineVar *clvPtr = &commandLineVars[0];
	while(clvPtr->varName) {
		if(strcasecmp(varName, clvPtr->varName) == 0)
			clvPtr->varValue = varValue;
		++clvPtr;
	}
}

char *
getCmdLineVar(char *varName)
{
	CmdLineVar *clvPtr = &commandLineVars[0];
	while(clvPtr->varName) {
		if(strcasecmp(varName, clvPtr->varName) == 0)
			return(clvPtr->varValue);
		++clvPtr;
	}
	return((char *) 0);
}

static char *
makeLogFileName(void)
{
	static char tbuf[64];

	tp = time(NULL);
	(void) strftime(tbuf, sizeof(tbuf), "%Y%m%d_%H%M%S.txt", localtime(&tp));
	return(tbuf);
}


static void
getParameters(char *params)
{
	char *paramVal, *s1;
	char *variables[128];
	int vc, x, sc;
	char tbuf[64];
	DateTime dt;
	

	/////////////////////////////////////////////////////////////////////////////////
	// We will not retrieve ALL the parameters from the parameter file here.
	/////////////////////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////
	// Get the LogFile directory and open the log file
	///////////////////////////////////////////////////
	if(! (paramVal = getCmdLineVar("logDir"))) {
		if(!(paramVal = getParamValue("logDir"))) {
			fprintf(stderr,"Required parameter \"logDir\" missing from parameter file %s\n", params);
			exit(1);
		}
	}
	if((_access(paramVal, 0)) == -1 ) {
		if(_mkdir(paramVal) == -1) {
			fprintf(stderr,"Unable to create log file directory %s\n%s\n", paramVal, strerror(errno));
			exit(1);
		}
	}
	sprintf(logFileName,"%s\\%s", paramVal, makeLogFileName());
	if((logFile = fopen(logFileName,"w")) == NULL) {
		fprintf(stderr,"can't open %s\n%s\n", logFileName, strerror(errno));
		exit(1);
	}
	logMsg(logFile,"CSHOpt_main started ....\n\n");
	logMsg(logFile,"runtime (may be fake runtime) in GMT: %s\n\n", dt_DateTimeToDateTimeString(dt_run_time_GMT,tbuf,"%Y/%m/%d %H:%M"));
	
	//if((summaryFile = fopen("./Logfiles/summaryLog.txt","a")) == NULL) {
	//	fprintf(stderr,"can't open summaryFile\n%s\n", strerror(errno));
	//	exit(1);
	//}

	////////////////////////////////////////
	// Get the database login information
	////////////////////////////////////////
	/* required: "pdataPath - path to pdata.txt which contains db login info */
	if(! (pdataPath = getCmdLineVar("pdataPath"))) {
		if(! (pdataPath = getParamValue("pdataPath"))) {
			logMsg(logFile,"Required parameter \"pdataPath\" missing from parameter file %s\n", params);
			exit(1);
		}
	}

	if(! loadParams(pdataPath)) {
		logMsg(logFile,"Can't get login information from file %s\n", pdataPath);
		exit(1);
	}

	/* required: "mysqlServer" */
	if(! (mysqlServer = getParamValue("mysqlServer"))) {
		logMsg(logFile,"Required parameter \"mysqlServer\" missing from file %s\n", pdataPath);
		exit(1);
	}

	/* required: "mysqlUser" */
	if(! (mysqlUser = getParamValue("mysqlUser"))) {
		logMsg(logFile,"Required parameter \"mysqlUser\" missing from file %s\n", pdataPath);
		exit(1);
	}

	/* required: "mysqlPW" */
	if(! (mysqlPW = getParamValue("mysqlPW"))) {
		logMsg(logFile,"Required parameter \"mysqlPW\" missing from file %s\n", pdataPath);
		exit(1);
	}

	/* required: "mysqlServer_oag" */
	if(! (mysqlServer_oag = getParamValue("mysqlServer_oag"))) {
		logMsg(logFile,"Required parameter \"mysqlServer_oag\" missing from file %s\n", pdataPath);
		exit(1);
	}

	/* required: "mysqlUser_oag" */
	if(! (mysqlUser_oag = getParamValue("mysqlUser_oag"))) {
		logMsg(logFile,"Required parameter \"mysqlUser_oag\" missing from file %s\n", pdataPath);
		exit(1);
	}

	/* required: "mysqlPW_oag" */
	if(! (mysqlPW_oag = getParamValue("mysqlPW_oag"))) {
		logMsg(logFile,"Required parameter \"mysqlPW_oag\" missing from file %s\n", pdataPath);
		exit(1);
	}

	/* required: "remoteMysqlServer" */
	if(! (remoteMysqlServer = getParamValue("remoteMysqlServer"))) {
		logMsg(logFile,"Required parameter \"remoteMysqlServer\" missing from file %s\n", pdataPath);
		exit(1);
	}

	/* required: "remotemysqlUser" */
	if(! (remoteMysqlUser = getParamValue("remoteMysqlUser"))) {
		logMsg(logFile,"Required parameter \"remoteMysqlUser\" missing from file %s\n", pdataPath);
		exit(1);
	}

	/* required: "remoteMysqlPW" */
	if(! (remoteMysqlPW = getParamValue("remoteMysqlPW"))) {
		logMsg(logFile,"Required parameter \"remoteMysqlPW\" missing from file %s\n", pdataPath);
		exit(1);
	}

	/* required: "database" */
	if(! (database = getParamValue("database"))) {
		logMsg(logFile,"Required parameter \"database\" missing from parameter file %s\n", params);
		exit(1);
	}
	/* required: "database_oag" */
	if(! (database_oag = getParamValue("database_oag"))) {
		logMsg(logFile,"Required parameter \"database_oag\" missing from parameter file %s\n", params);
		exit(1);
	}

	/* required: "remotedatabase" */
	if(! (remotedatabase = getParamValue("remotedatabase"))) {
		logMsg(logFile,"Required parameter \"remotedatabase\" missing from parameter file %s\n", params);
		exit(1);
	}
    
	if(oracleDirect == 2)
	{  if(! (oracleServer = getParamValue("oracleServer"))) {
		 logMsg(logFile,"Required parameter \"oracleServer\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	   if(! (oracleUser = getParamValue("oracleUser"))) {
		 logMsg(logFile,"Required parameter \"oracleUser\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	   if(! (oraclePW = getParamValue("oraclePW"))) {
		 logMsg(logFile,"Required parameter \"oraclePW\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	   if(! (oracleDB = getParamValue("oracleDB"))) {
		 logMsg(logFile,"Required parameter \"oracleDB\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	   if(! (oracleServerSS = getParamValue("oracleServerSS"))) {
		 logMsg(logFile,"Required parameter \"oracleServerSS\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	   if(! (oracleUserSS = getParamValue("oracleUserSS"))) {
		 logMsg(logFile,"Required parameter \"oracleUserSS\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	   if(! (oraclePWSS = getParamValue("oraclePWSS"))) {
		 logMsg(logFile,"Required parameter \"oraclePWSS\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	   if(! (oracleDBSS = getParamValue("oracleDBSS"))) {
		 logMsg(logFile,"Required parameter \"oracleDBSS\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	}	
	else if (optParam.paramsOracle == 1){
	   if(! (oracleServer = getParamValue("oracleServer"))) {
		 logMsg(logFile,"Required parameter \"oracleServer\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	   if(! (oracleUser = getParamValue("oracleUser"))) {
		 logMsg(logFile,"Required parameter \"oracleUser\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	   if(! (oraclePW = getParamValue("oraclePW"))) {
		 logMsg(logFile,"Required parameter \"oraclePW\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	   if(! (oracleDB = getParamValue("oracleDB"))) {
		 logMsg(logFile,"Required parameter \"oracleDB\" missing from parameter file %s\n", params);
		 exit(1);
	   }
	}
	
	//////////////////////////////////
	// See if we want verbose logging
	//////////////////////////////////
	/* optional: verbose logging */
	if((paramVal = getParamValue("verbose"))) {
		if(strncasecmp(paramVal,"Y",1) == 0)
			verbose = 1;
	}

	////////////////////////////////////////////////////////
	// see what kind of type 3 exclusions we should create
	////////////////////////////////////////////////////////
	if((paramVal = getParamValue("type_3_exclusions"))) {
		if(strncasecmp(paramVal,"WET",1) == 0) {
			wetDry = WET;
		}
		else if(strncasecmp(paramVal,"DRY",1) == 0)
			wetDry = DRY;
		else {
			logMsg(logFile,"\"%s\" is not a valid value for parameter \"type_3_exclusions\" in parameter file %s\n",
				paramVal, params);
			exit(1);
		}
			
	}
	else {
		logMsg(logFile,"Required parameter \"type_3_exclusions\" missing from parameter file %s\n", params);
		exit(1);
	}

	///////////////////////////////////////////////////
	// get the SQL statements from the parameter file
	///////////////////////////////////////////////////

	///////////////////////////
	// required: "demandSQL" //
	///////////////////////////

	logMsg(logFile,"optParam.includeLockedUmlg = %d\n", optParam.includeLockedUmlg);

	if(optParam.includeLockedUmlg == 1) {
		if(! (demandSQL = getParamValue("allDemandSQL"))) {
			logMsg(logFile,"Required parameter \"allDemandSQL\" missing from parameter file %s\n", params);
			exit(1);
		}
	} else {
		if(! (demandSQL = getParamValue("demandSQL"))) {
			logMsg(logFile,"Required parameter \"demandSQL\" missing from parameter file %s\n", params);
			exit(1);
		}
	}
	//if(! (demandSQL = getParamValue("demandSQL"))) {
	//	logMsg(logFile,"Required parameter \"demandSQL\" missing from parameter file %s\n", params);
	//	exit(1);
	//}
	vc = getVars(demandSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(demandSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		demandSQL = strdup(s1);
		if(!demandSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"demandSQL:\n%s\n\n", demandSQL);

	///////////////////////////
	// Optional: "demandnumbyzoneSQL" //
	///////////////////////////
    if(! (demandnumbyzoneSQL = getParamValue("demandnumbyzoneSQL"))) {
		logMsg(logFile,"Required parameter \"demandnumbyzoneSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
    
	////////////
	//Jintao's demandnumSQL test
	//logMsg(logFile, "demandnumbyzoneSQL%s",demandnumbyzoneSQL);
	//vc = getVars(demandnumbyzoneSQL, 128, variables);
	////////////

	///////////////////////////
	// required: "legSQL" //
	///////////////////////////
	if(! (legSQL = getParamValue("legSQL"))) {
		logMsg(logFile,"Required parameter \"legSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(legSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(legSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		legSQL = strdup(s1);
		if(!legSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"legSQL:\n%s\n\n", legSQL);

	
	///////////////////////////
	// required: "fuelLegSQL" //
	///////////////////////////

	if(! (fuelLegSQL = getParamValue("fuelLegSQL"))) {
		logMsg(logFile,"Required parameter \"fuelLegSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(fuelLegSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(fuelLegSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		fuelLegSQL = strdup(s1);
		if(!fuelLegSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"fuelLegSQL:\n%s\n\n", fuelLegSQL);



	//////////////////////////////////////
	// required: "whereAreThePlanesSQL" //
	//////////////////////////////////////
	if(! (whereAreThePlanesSQL = getParamValue("whereAreThePlanesSQL"))) {
		logMsg(logFile,"Required parameter \"whereAreThePlanesSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(whereAreThePlanesSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(whereAreThePlanesSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		whereAreThePlanesSQL = strdup(s1);
		if(!whereAreThePlanesSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"whereAreThePlanesSQL:\n%s\n\n", whereAreThePlanesSQL);


	////////////////////////////
	// required: "mtcnoteSQL" //
	////////////////////////////
	if(! (mtcnoteSQL = getParamValue("mtcnoteSQL"))) {
		logMsg(logFile,"Required parameter \"mtcnoteSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(mtcnoteSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(mtcnoteSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		mtcnoteSQL = strdup(s1);
		if(!mtcnoteSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}

	if(verbose)
		logMsg(logFile,"mtcnoteSQL:\n%s\n\n", mtcnoteSQL);

	////////////////////////////
	// required: "airportSQL" //
	////////////////////////////
	if(! (airportSQL = getParamValue("airportSQL"))) {
		logMsg(logFile,"Required parameter \"airportSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(airportSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(airportSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		airportSQL = strdup(s1);
		if(!airportSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}

	if(verbose)
		logMsg(logFile,"airportSQL:\n%s\n\n", airportSQL);

	////////////////////////////
	// required: "exclusionSQL" //
	////////////////////////////
	if(! (exclusionSQL = getParamValue("exclusionSQL"))) {
		logMsg(logFile,"Required parameter \"exclusionSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(exclusionSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(exclusionSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		exclusionSQL = strdup(s1);
		if(!exclusionSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}

	if(verbose)
		logMsg(logFile,"exclusionSQL:\n%s\n\n", exclusionSQL);

	////////////////////////////
	// required: "curfewexclusionSQL" //
	////////////////////////////
	if(! (curfewexclusionSQL = getParamValue("curfewexclusionSQL"))) {
		logMsg(logFile,"Required parameter \"curfewexclusionSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(curfewexclusionSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(curfewexclusionSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		curfewexclusionSQL = strdup(s1);
		if(!curfewexclusionSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}

	if(verbose)
		logMsg(logFile,"curfewexclusionSQL:\n%s\n\n", curfewexclusionSQL);

	////////////////////////////
	// required: crewIdSQL
	////////////////////////////
	if(! (crewIdSQL = getParamValue("crewIdSQL"))) {
		logMsg(logFile,"Required parameter \"crewIdSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(crewIdSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(crewIdSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		crewIdSQL = strdup(s1);
		if(!crewIdSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"crewIdSQL:\n%s\n\n", crewIdSQL);


    ////////////////////////////
	// required: bwCrewPairDataSQL  Roger's fix for hasFlownFirst
	////////////////////////////
	if(! (bwCrewPairDataSQL = getParamValue("bwCrewPairDataSQL"))) {
		logMsg(logFile,"Required parameter \"bwCrewPairDataSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(bwCrewPairDataSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(bwCrewPairDataSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		bwCrewPairDataSQL = strdup(s1);
		if(!bwCrewPairDataSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"bwCrewPairDataSQL:\n%s\n\n", bwCrewPairDataSQL);










	////////////////////////////
	// required: bwCrewDataSQL
	////////////////////////////
	if(! (bwCrewDataSQL = getParamValue("bwCrewDataSQL"))) {
		logMsg(logFile,"Required parameter \"bwCrewDataSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(bwCrewDataSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(bwCrewDataSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		bwCrewDataSQL = strdup(s1);
		if(!bwCrewDataSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"bwCrewDataSQL:\n%s\n\n", bwCrewDataSQL);

	////////////////////////////
	// required: csCrewDataSQL
	////////////////////////////
	if(! (csCrewDataSQL = getParamValue("csCrewDataSQL"))) {
		logMsg(logFile,"Required parameter \"csCrewDataSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(csCrewDataSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(csCrewDataSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		csCrewDataSQL = strdup(s1);
		if(!csCrewDataSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"csCrewDataSQL:\n%s\n\n", csCrewDataSQL);

	////////////////////////////
	// required: ssCrewDataSQL
	////////////////////////////
	if(! (ssCrewDataSQL = getParamValue("ssCrewDataSQL"))) {
		logMsg(logFile,"Required parameter \"ssCrewDataSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(ssCrewDataSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(ssCrewDataSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		ssCrewDataSQL = strdup(s1);
		if(!ssCrewDataSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"ssCrewDataSQL:\n%s\n\n", ssCrewDataSQL);

	////////////////////////////
	// required: charterStatsSQL
	////////////////////////////
	if(! (charterStatsSQL = getParamValue("charterStatsSQL"))) {
		logMsg(logFile,"Required parameter \"charterStatsSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(charterStatsSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(charterStatsSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		charterStatsSQL = strdup(s1);
		if(!charterStatsSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"charterStatsSQL:\n%s\n\n", charterStatsSQL);

	/////////////////////////////////
	// required: pairConstraintsSQL
	/////////////////////////////////
	if(! (pairConstraintsSQL = getParamValue("pairConstraintsSQL"))) {
		logMsg(logFile,"Required parameter \"pairConstraintsSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(pairConstraintsSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(pairConstraintsSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		pairConstraintsSQL = strdup(s1);
		if(!pairConstraintsSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"pairConstraintsSQL:\n%s\n\n", pairConstraintsSQL);

	/////////////////////////////////
	// required: intnlCertSQL
	/////////////////////////////////
	if(! (intnlCertSQL = getParamValue("intnlCertSQL"))) {
		logMsg(logFile,"Required parameter \"intnlCertSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(intnlCertSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(intnlCertSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		intnlCertSQL = strdup(s1);
		if(!intnlCertSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"intnlCertSQL:\n%s\n\n", intnlCertSQL);

	///////////////////////////
	// required: "statSQL" //
	///////////////////////////

	//statSQL - 02/12/08 ANG
	if(! (statSQL = getParamValue("statSQL"))) {
		logMsg(logFile,"Required parameter \"statSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	
	//START - For break down stats by day - 12/05/08 ANG
	statSQLD1 = getParamValue("statSQL");
	statSQLD2 = getParamValue("statSQL");
	statSQLD3 = getParamValue("statSQL");
	//END - 12/05/08 ANG

	vc = getVars(statSQL, 256, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(statSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		statSQL = strdup(s1);
		if(!statSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}

	///////////////////////////
	// required: "statSchedSQL" //
	///////////////////////////

	//statSchedSQL - 11/10/08 ANG
	if(! (statSchedSQL = getParamValue("statSchedSQL"))) {
		logMsg(logFile,"Required parameter \"statSchedSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	//logMsg(logFile, "statSchedSQL = %s", statSchedSQL);

	//START - For break down stats by day - 12/08/08 ANG
	statSchedSQLD1 = getParamValue("statSchedSQL");
	statSchedSQLD2 = getParamValue("statSchedSQL");
	statSchedSQLD3 = getParamValue("statSchedSQL");
	//END - 12/08/08 ANG

	vc = getVars(statSchedSQL, 256, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(statSchedSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		statSchedSQL = strdup(s1);
		if(!statSchedSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}

	//statSchedSQL - 11/10/08 ANG
	if(! (statSchedSQL2 = getParamValue("statSchedSQL2"))) {
		logMsg(logFile,"Required parameter \"statSchedSQL2\" missing from parameter file %s\n", params);
		exit(1);
	}
	//logMsg(logFile, "statSchedSQL2 = %s", statSchedSQL2);

	//START - For break down stats by day - 12/08/08 ANG
	statSchedSQL2D1 = getParamValue("statSchedSQL2");
	statSchedSQL2D2 = getParamValue("statSchedSQL2");
	statSchedSQL2D3 = getParamValue("statSchedSQL2");
	//END - 12/08/08 ANG

	vc = getVars(statSchedSQL2, 256, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(statSchedSQL2,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		statSchedSQL2 = strdup(s1);
		if(!statSchedSQL2) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}

	//statSchedSQL - 11/10/08 ANG
	if(! (statSchedSQL3 = getParamValue("statSchedSQL3"))) {
		logMsg(logFile,"Required parameter \"statSchedSQL3\" missing from parameter file %s\n", params);
		exit(1);
	}
	//logMsg(logFile, "statSchedSQL3 = %s", statSchedSQL3);

	//START - For break down stats by day - 12/08/08 ANG
	statSchedSQL3D1 = getParamValue("statSchedSQL3");
	statSchedSQL3D2 = getParamValue("statSchedSQL3");
	statSchedSQL3D3 = getParamValue("statSchedSQL3");
	//END - 12/08/08 ANG

	vc = getVars(statSchedSQL3, 256, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(statSchedSQL3,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		statSchedSQL3 = strdup(s1);
		if(!statSchedSQL3) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}

	///////////////////////////
	// required: "macStatSQL" //
	///////////////////////////

	//macStatSQL - 02/12/08 ANG
	if(! (macStatSQL = getParamValue("macStatSQL"))) {
		logMsg(logFile,"Required parameter \"macStatSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(macStatSQL, 256, variables);

	//START - For break down stats by day - 12/08/08 ANG
	macStatSQLD1 = getParamValue("macStatSQL");
	macStatSQLD2 = getParamValue("macStatSQL");
	macStatSQLD3 = getParamValue("macStatSQL");
	//END - 12/08/08 ANG

	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(macStatSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		macStatSQL = strdup(s1);
		if(!macStatSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}

	///////////////////////////
	// required: "macStatSQL2" //
	///////////////////////////

	//macStatSQL2 - 02/12/08 ANG
	if(! (macStatSQL2 = getParamValue("macStatSQL2"))) {
		logMsg(logFile,"Required parameter \"macStatSQL2\" missing from parameter file %s\n", params);
		exit(1);
	}

	//START - For break down stats by day - 12/08/08 ANG
	macStatSQL2D1 = getParamValue("macStatSQL2");
	macStatSQL2D2 = getParamValue("macStatSQL2");
	macStatSQL2D3 = getParamValue("macStatSQL2");
	//END - 12/08/08 ANG

	vc = getVars(macStatSQL2, 256, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(macStatSQL2,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		macStatSQL2 = strdup(s1);
		if(!macStatSQL2) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}

	/////////////////////////////////////////////////
	// required: "macInfoSQL"  - MAC - 09/02/08 ANG//
	/////////////////////////////////////////////////

	if(! (macInfoSQL = getParamValue("macInfoSQL"))) {
		logMsg(logFile,"Required parameter \"macInfoSQL\" missing from parameter file %s\n", params);
		exit(1);
	}
	vc = getVars(macInfoSQL, 128, variables);
	for(x = 0; x < vc; ++x) {
		if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(macInfoSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		macInfoSQL = strdup(s1);
		if(!macInfoSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	}
	if(verbose)
		logMsg(logFile,"macInfoSQL:\n%s\n\n", macInfoSQL);


	/////////////////////////////////
	// required: macInfoOracleSQL
	/////////////////////////////////
	if(oracleDirect != 0){
	   if(! (macInfoOracleSQL = getParamValue("macInfoOracleSQL"))) {
		   logMsg(logFile,"Required parameter \"macInfoOracleSQL\" missing from parameter file %s\n", params);
		    exit(1);
	    }
	   vc = getVars(macInfoOracleSQL, 128, variables);
	   for(x = 0; x < vc; ++x) {
		 if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(macInfoOracleSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		macInfoOracleSQL = strdup(s1);
		if(!macInfoOracleSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	   }
	    if(verbose)
		   logMsg(logFile,"macInfoOracleSQL:\n%s\n\n", macInfoOracleSQL);
	}

    /////////////////////////////////
	// required: peakdayscontractratesOracleSQL
	/////////////////////////////////
	if(oracleDirect == 2){
	   if(! (peakdayscontractratesOracleSQL = getParamValue("peakdayscontractratesOracleSQL"))) {
		   logMsg(logFile,"Required parameter \"peakdayscontractratesOracleSQL\" missing from parameter file %s\n", params);
		    exit(1);
	    }
	   vc = getVars(peakdayscontractratesOracleSQL, 128, variables);
	   for(x = 0; x < vc; ++x) {
		 if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(peakdayscontractratesOracleSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		peakdayscontractratesOracleSQL = strdup(s1);
		if(!peakdayscontractratesOracleSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	   }
	    if(verbose)
		   logMsg(logFile,"peakdayscontractratesOracleSQL:\n%s\n\n", peakdayscontractratesOracleSQL);
	}

	 /////////////////////////////////
	// required: csCrewDataOracleSQL
	/////////////////////////////////
	if(oracleDirect == 2){
	   if(! (csCrewDataOracleSQL = getParamValue("csCrewDataOracleSQL"))) {
		   logMsg(logFile,"Required parameter \"csCrewDataOracleSQL\" missing from parameter file %s\n", params);
		    exit(1);
	    }
	   vc = getVars(csCrewDataOracleSQL, 128, variables);
	   for(x = 0; x < vc; ++x) {
		 if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(csCrewDataOracleSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		csCrewDataOracleSQL = strdup(s1);
		if(!csCrewDataOracleSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	   }
	    if(verbose)
		   logMsg(logFile,"csCrewDataOracleSQL:\n%s\n\n", csCrewDataOracleSQL);
	}
    
	/////////////////////////////////
	// required: ssCrewDataOracleSQL
	/////////////////////////////////
   if(oracleDirect == 2){
	   if(! (ssCrewDataOracleSQL = getParamValue("ssCrewDataOracleSQL"))) {
		   logMsg(logFile,"Required parameter \"ssCrewDataOracleSQL\" missing from parameter file %s\n", params);
		    exit(1);
	    }
	   vc = getVars(ssCrewDataOracleSQL, 128, variables);
	   for(x = 0; x < vc; ++x) {
		 if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(ssCrewDataOracleSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		ssCrewDataOracleSQL = strdup(s1);
		if(!ssCrewDataOracleSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	   }
	    if(verbose)
		   logMsg(logFile,"ssCrewDataOracleSQL:\n%s\n\n", ssCrewDataOracleSQL);
	}

    /////////////////////////////////
	// required: ratioOracleSQL
	/////////////////////////////////
   if(oracleDirect == 2){
	   if(! (ratioOracleSQL = getParamValue("ratioOracleSQL"))) {
		   logMsg(logFile,"Required parameter \"ratioOracleSQL\" missing from parameter file %s\n", params);
		    exit(1);
	    }
	   vc = getVars(ratioOracleSQL, 128, variables);
	   for(x = 0; x < vc; ++x) {
		 if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(ratioOracleSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		ratioOracleSQL = strdup(s1);
		if(!ratioOracleSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	   }
	    if(verbose)
		   logMsg(logFile,"ratioOracleSQL:\n%s\n\n", ratioOracleSQL);
	} 

    /////////////////////////////////
	// required: upgradeDowngradeOracleSQL
	/////////////////////////////////
   if(oracleDirect == 2){
	   if(! (upgradeDowngradeOracleSQL = getParamValue("upgradeDowngradeOracleSQL"))) {
		   logMsg(logFile,"Required parameter \"upgradeDowngradeOracleSQL\" missing from parameter file %s\n", params);
		    exit(1);
	    }
	   vc = getVars(upgradeDowngradeOracleSQL, 128, variables);
	   for(x = 0; x < vc; ++x) {
		 if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(upgradeDowngradeOracleSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		upgradeDowngradeOracleSQL = strdup(s1);
		if(!upgradeDowngradeOracleSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	   }
	    if(verbose)
		   logMsg(logFile,"upgradeDowngradeOracleSQL:\n%s\n\n", upgradeDowngradeOracleSQL);
	} 
    
   /////////////////////////////////
	// required: timezoneOracleSQL
	/////////////////////////////////
   if(oracleDirect == 2){
	   if(! (timezoneOracleSQL = getParamValue("timezoneOracleSQL"))) {
		   logMsg(logFile,"Required parameter \"timezoneOracleSQL\" missing from parameter file %s\n", params);
		    exit(1);
	    }
	   vc = getVars(timezoneOracleSQL, 128, variables);
	   for(x = 0; x < vc; ++x) {
		 if(! (paramVal = getCmdLineVar(*(variables + x)))) {
			if(!(paramVal = getParamValue(*(variables + x)))) {
				logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
				exit(1);
			}
		}
		s1 = substitute(timezoneOracleSQL,makevar(*(variables + x)), paramVal, &sc);
		if(!s1) {
			logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
			exit(1);
		}
		timezoneOracleSQL = strdup(s1);
		if(!timezoneOracleSQL) {
			logMsg(logFile,"Out of memory.\n");
			exit(1);
		}
	   }
	    if(verbose)
		   logMsg(logFile,"timezoneOracleSQL:\n%s\n\n", timezoneOracleSQL);
	} 

   /////////////////////////////////
	// required: cstravelSQL
	/////////////////////////////////
   /* required: "travelcutoff" - 09/11/08 ANG*/
	if(!(paramVal = getCmdLineVar("travelcutoff"))) {
	  if(!(paramVal = getParamValue("travelcutoff"))) {
		logMsg(logFile,"Required parameter \"travelcutoff\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.travelcutoff = atoi(paramVal);
	//if(verbose) fprintf(logFile,"%27s = %5d\n", "travelcutoff", optParam.travelcutoff);

	/* required: "sendhomecutoff" - 04/20/09 Jintao*/
	if(!(paramVal = getCmdLineVar("sendhomecutoff"))) {
	  if(!(paramVal = getParamValue("sendhomecutoff"))) {
		logMsg(logFile,"Required parameter \"sendhomecutoff\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
	optParam.sendhomecutoff = atoi(paramVal);
	if(verbose) fprintf(logFile,"%27s = %5d\n", "sendhomecutoff", optParam.sendhomecutoff);


	if(oracleDirect == 0){
		if(! (cstravelSQL = getParamValue("cstravelSQL"))) {
			logMsg(logFile,"Required parameter \"cstravelSQL\" missing from parameter file %s\n", params);
			exit(1);
		}
		vc = getVars(cstravelSQL, 128, variables);
		for(x = 0; x < vc; ++x) {
			//dt = dt_addToDateTime(Hours, optParam.travelcutoff, dt_run_time_GMT); //need to get travelcutoff into parameters file
			//strcpy(paramVal, dt_DateTimeToDateTimeString(dt_run_time_GMT, tbuf, "%Y/%m/%d %H:%M"));
			//strcpy(paramVal, dt_DateTimeToDateTimeString(dt, tbuf, "%Y/%m/%d %H:%M"));
			if(! (paramVal = getCmdLineVar(*(variables + x)))) {
				if(!(paramVal = getParamValue(*(variables + x)))) {
					logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
					exit(1);
				}
			}
			s1 = substitute(cstravelSQL,makevar(*(variables + x)), paramVal, &sc);
			if(!s1) {
				logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
				exit(1);
			}
			cstravelSQL = strdup(s1);
			if(!cstravelSQL) {
				logMsg(logFile,"Out of memory.\n");
				exit(1);
			}
		}
		if(verbose)
			logMsg(logFile,"cstravelSQL:\n%s\n\n", cstravelSQL);
	}

   /////////////////////////////////
	// required: cstravelOracleSQL
	/////////////////////////////////


   if(oracleDirect != 0){
		if(! (cstravelOracleSQL = getParamValue("cstravelOracleSQL"))) {
			logMsg(logFile,"Required parameter \"cstravelOracleSQL\" missing from parameter file %s\n", params);
			exit(1);
		}
		vc = getVars(cstravelOracleSQL, 128, variables);
		for(x = 0; x < vc; ++x) {
			//dt = dt_addToDateTime(Hours, optParam.travelcutoff, dt_run_time_GMT); //need to get travelcutoff into parameters file
			//strcpy(paramVal, dt_DateTimeToDateTimeString(dt_run_time_GMT, tbuf, "%Y/%m/%d %H:%M"));
			//strcpy(paramVal, dt_DateTimeToDateTimeString(dt, tbuf, "%Y/%m/%d %H:%M"));
			if(! (paramVal = getCmdLineVar(*(variables + x)))) {
				if(!(paramVal = getParamValue(*(variables + x)))) {
					logMsg(logFile,"Required parameter \"%s\" missing from parameter file %s\n", *(variables + x), params);
					exit(1);
				}
			}
			s1 = substitute(cstravelOracleSQL,makevar(*(variables + x)), paramVal, &sc);
			if(!s1) {
				logMsg(logFile,"substitution failed: substitute() returned null, substitution count = %d\n", sc);
				exit(1);
			}
			cstravelOracleSQL = strdup(s1);
			if(!cstravelOracleSQL) {
				logMsg(logFile,"Out of memory.\n");
				exit(1);
			}
		}
		if(verbose)
			logMsg(logFile,"cstravelOracleSQL:\n%s\n\n", cstravelOracleSQL);

   }

}







static void
getStartWin(DateTime inputDateDT, time_t *windowStart, time_t *windowEnd)
{
	time_t endOfDay;
	DateTime windowStartDT;
	DateTimeParts dtparts;
	DateTime priorMidnightDT;

	if(!inputDateDT)
		*windowStart = time(NULL) + (optParam.planningWindowStart * 60);
	else
		*windowStart = DateTimeToTime_t(inputDateDT);

	windowStartDT = dt_time_tToDateTime(*windowStart);

	// get prior midnight
	dt_DateTimeToDateTimeParts(windowStartDT, &dtparts);
	if(! (dtparts.tparts.hour == 0 && dtparts.tparts.min == 0 && dtparts.tparts.sec == 0))
		priorMidnightDT = dt_MDYHMSMtoDateTime((int) dtparts.dparts.month, (int) dtparts.dparts.day, (int) dtparts.dparts.year, 0, 0, 0, 0);
	else
		priorMidnightDT = windowStartDT;

	endOfDay = DateTimeToTime_t(priorMidnightDT) + (optParam.dayEndTime * 60); 
	while (endOfDay <= *windowStart) 
		endOfDay = endOfDay + (24 * 60 * 60);

	*windowEnd = endOfDay + ((optParam.planningWindowDuration -1) * (24 * 60 * 60));

}
