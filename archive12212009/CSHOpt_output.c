#include "os_config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <search.h>
#include <string.h>
#include "datetime.h"
#include "bintree.h"
#include "my_mysql.h"
#include "queue.h"
#include "logMsg.h"
#include "cjoin.h"
#include "CSHOpt_struct.h"
#include "CSHOpt_output.h"
#include "CSHOpt_readInput.h"
#include "CSHOpt_scheduleSolver.h"

int local_scenarioid = 0;
int remote_scenarioid = 0;
char host_name[128];
char ip_addr[24];

extern FILE *logFile;
extern ProposedCrewAssg *propCrewAssignment;
extern int numPropCrewDays;

extern ProposedMgdLeg *propMgdLegs;
extern int numPropMgdLegs;

extern ProposedUnmgdLeg *propUnmgdLegs;
extern int numPropUnmgdLegs;
extern MY_CONNECTION *myconn;
extern MY_CONNECTION *remotemyconn;
extern MY_CONNECTION *myconn_oag;
extern OptParameters optParam;
extern Warning_error_Entry *errorinfoList;
extern int errorNumber;
extern int  local_scenarioid;
extern BINTREENODE *travelRqstRoot;
extern int travelRqsts;
extern Airport2 *aptList; 
extern DateTime dt_run_time_GMT; // run time in dt_ format in GMT

BINTREENODE *concurr_flightRoot = NULL;
BINTREENODE *insert_flightRoot = NULL;
BINTREENODE *resultRoot = NULL;
int *concurr_crewListEffected;
int concurr_crewnumbereffected = 0;
static BINTREENODE *leg1Root = NULL;


typedef struct generatedcstravel
{
	int travellerid;
	int leg1_dpt_aptid;
	int leg1_arr_aptid;
	char *leg1_dpt_time;
	char *leg1_arr_time;
	int leg2_dpt_aptid;
	int leg2_arr_aptid;
	char *leg2_dpt_time;
	char *leg2_arr_time;
    int requestid;
	int directflight; //indicate if direct flight or not
	int groundtravl; //indicate this travel is groundtravel or not
	int leg0_dpt_aptid; 
	int leg0_arr_aptid;
	char *leg0_dpt_time;
	char *leg0_arr_time;
    int leg3_dpt_aptid; 
	int leg3_arr_aptid;
	char *leg3_dpt_time;
	char *leg3_arr_time;  
	int groundtravel1;
	int groundtravel2;
	int flightpurpose;
	int tix_rqstid_cancelled;
	//leg 0: ground travel for first commercial leg
	//leg 1: the first leg in one-stop travel
	//leg 2: the second leg in one-stop travel
	//leg 3: ground travel for the second commercial leg
}GeneratedCsTravel;

typedef struct leg1 {
	int dep_aptid;
	int arr_aptid;
	char FD_airline_desig[4];
	char FD_flight_number[8];
	DateTime UTC_departure_time;
	DateTime UTC_arrival_time;
} LEG1;

/*Jintao's functions */

static int travelFlightCmp(void *a1, void *b1);
void updateCsTravelFlights(MY_CONNECTION *conn, int scenarioid);
void generateOneTravelRecord(int crewID, int request_dep_aptID, int request_arr_aptID, char *request_earliest_dep, char *request_latest_arr, int request_flight_purpose, int updatetype, int request_groundtravl, int tix_rqstid_cancelled, int *maxrequestid);
void updateOneTravelRecord(GeneratedCsTravel *gencstrlPtr, int flight_purpose);
void insertOneTravelRecord(GeneratedCsTravel *gencstrlPtr);
int cjoin1(MY_CONNECTION *myconn, BINTREENODE **result, char *oag1sql, char *oag2sql, int MIN_Layover, int MAX_Layover);
void writeProposedTravelFlights(MY_CONNECTION *conn, int scenarioid);
void adjOverlapedCrewAssignments(MY_CONNECTION *myconn, int scenarioid, time_t plwinstarttime);
static int myleg1Compare(void *a, void *b);
static int myresultCompare(void *a, void *b);
static void myshowLeg1(LEG1 *leg1Ptr);


/********************************************************************************
*	Function:  writeOutputData, updated JT 05/04/2009
*	Purpose:  write out solution to db
********************************************************************************/
void
writeOutputData(MY_CONNECTION *conn, int scenarioid)
{
	int i;
	char sqlBuf[1024];
	char opbuf1[32], opbuf2[32];
	DateTime dt1, dt2;
	char demandidBuf[32];
	char schedoutfboidBuf[32];
	char schedinfboidBuf[32];
	char off_acID_str[32], on_acID_str[32], requestid_cancelled_str[32];
	TravelRequest *trlrqtPtr;
	BINTREENODE *tmp;

	if(! conn) {
		logMsg(logFile,"%s Line %d: no MySQL connection!\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if(! scenarioid) {
		logMsg(logFile,"%s Line %d: no scenarioid!\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
/*
managedlegid int(11) primary key not null auto_increment,
demandid int(11) default NULL,
schedoutfboid int(11) default NULL,
schedinfboid int(11) default NULL,
schedoutaptid int(11) NOT NULL,
schedinaptid int(11) NOT NULL,
aircraftid int(11) NOT NULL,
scenarioid int(11) NOT NULL,
schedout datetime NOT NULL,
schedin datetime NOT NULL,
*/
	for (i=0; i<numPropMgdLegs; i++) {

		dt1 = dt_time_tToDateTime (propMgdLegs[i].schedOut);
		dt2 = dt_time_tToDateTime (propMgdLegs[i].schedIn);
		sprintf(demandidBuf,"%d", propMgdLegs[i].demandID);
		sprintf(schedoutfboidBuf,"%d", propMgdLegs[i].schedOutFBOID);
		sprintf(schedinfboidBuf,"%d", propMgdLegs[i].schedInFBOID);

		sprintf(sqlBuf, "insert into proposed_managedleg values(NULL,%s,%s,%s,%d,%d,%d,%d,'%s','%s')", //,%d)",
					(propMgdLegs[i].demandID) ? demandidBuf : "null", 
					(propMgdLegs[i].schedOutFBOID) ? schedoutfboidBuf : "null", 
					(propMgdLegs[i].schedInFBOID) ? schedinfboidBuf : "null", 
					propMgdLegs[i].schedOutAptID,
					propMgdLegs[i].schedInAptID,
					propMgdLegs[i].aircraftID,
					scenarioid,
					dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"),
					dt_DateTimeToDateTimeString(dt2, opbuf2, "%Y/%m/%d %H:%M") );
		          //  ,propMgdLegs[i].exgTour);

		if(!(myDoWrite(conn,sqlBuf))) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
			writeWarningData(myconn); exit(1);
		}
	}

/*
crewassignmentid int(11) primary key not null auto_increment,
crewid int(11) NOT NULL,
aircraftid int(11) NOT NULL,
scenarioid int(11) NOT NULL,
starttime DATETIME NOT NULL,
endtime DATETIME NOT NULL,
position int(11) NOT NULL,
*/

	for (i=0; i<numPropCrewDays; i++) {

		dt1 = dt_time_tToDateTime (propCrewAssignment[i].startTm);
		dt2 = dt_time_tToDateTime (propCrewAssignment[i].endTm);
		sprintf(sqlBuf, "insert into proposed_crewassignment values(NULL,%d,%d,%d,'%s','%s',%d)",
					propCrewAssignment[i].crewID,
					propCrewAssignment[i].aircraftID,
					scenarioid,
					dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"),
					dt_DateTimeToDateTimeString(dt2, opbuf2, "%Y/%m/%d %H:%M"),
					propCrewAssignment[i].position);

		if(!(myDoWrite(conn,sqlBuf))) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
			writeWarningData(myconn); exit(1);
		}
	}

/*
unmanagedlegid int(11) primary key not null auto_increment,
departureairportid int(11),
aircraftid int(11),
departurefboid int(11),
demandid int(11),
scheduledout datetime,
scheduledin datetime,
arrivalfboid int(11),
arrivalairportid int(11),
scenarioid int(11),
*/

	for (i=0; i<numPropUnmgdLegs; i++) {	
		dt1 = dt_time_tToDateTime (propUnmgdLegs[i].schedOut);
		dt2 = dt_time_tToDateTime (propUnmgdLegs[i].schedIn);
		sprintf(demandidBuf,"%d", propUnmgdLegs[i].demandID);
		sprintf(schedoutfboidBuf,"%d", propUnmgdLegs[i].departureFBOID);
		sprintf(schedinfboidBuf,"%d", propUnmgdLegs[i].arrivalFBOID);

		sprintf(sqlBuf, "insert into proposed_unmanagedleg values(null,%d,null,%s,%s,'%s','%s',%s,%d,%d)",
					propUnmgdLegs[i].departureAptID,
					(propUnmgdLegs[i].departureFBOID) ? schedoutfboidBuf : "null",
					(propUnmgdLegs[i].demandID) ? demandidBuf : "null", 
					dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"),
					dt_DateTimeToDateTimeString(dt2, opbuf2, "%Y/%m/%d %H:%M"),
					(propUnmgdLegs[i].arrivalFBOID) ? schedinfboidBuf : "null",
					propUnmgdLegs[i].arrivalAptID,
					scenarioid);

		if(!(myDoWrite(conn,sqlBuf))) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
			writeWarningData(myconn); exit(1);
		}
	}

	 /*
  travel_request_ID int(5) NOT NULL auto_increment,
  crewID int(8) NOT NULL,
  dept_aptID_travel int(5) default NULL,
  arr_aptID_travel int(5) default NULL,
  earliest_dept varchar(20) default NULL,
  latest_arr varchar(20) default NULL,
  flight_purpose int(3) default NULL,
  scenarioid int(11) NOT NUL
  */
	logMsg(logFile,"\n Travel requests:\n");
	fprintf(logFile, "flight_purpose: 1 for on tour, 2 for off tour, 3 for changing plane.\n");
	fprintf(logFile,"+--------------+--------------+------------------+--------------+------------------+-----------------+------------+-------------+-------------+-------------+------------------+-------------+----------------+\n");
	fprintf(logFile,"| CrewID       | Depart_AptID | Early_Dpt_time   | Arrive_AptID | Late_Arr_time    |  Flight_purpose |  Off_ACID  |   On_ACID   |   buyticket | cancel_tix  + rqtid_cancelled  + groundtravel+   Scenario_ID  |\n");
	fprintf(logFile,"+--------------+--------------+------------------+--------------+------------------+-----------------+------------+-------------+-------------+-------------+------------------+-------------+----------------+\n");
	if(travelRqstRoot){
	  for(tmp=Minimum(travelRqstRoot);tmp;tmp=Successor(tmp)){
		 trlrqtPtr = (TravelRequest *) getTreeObject(tmp);
		 //test
		 //  if(trlrqtPtr->groundtravel == 1)
		 //    continue;
		 //test
		 if(trlrqtPtr->earliest_dept >= trlrqtPtr->latest_arr)
			 continue;
         dt1 = dt_time_tToDateTime(trlrqtPtr->earliest_dept);
		 dt2 = dt_time_tToDateTime(trlrqtPtr->latest_arr);
		 if(trlrqtPtr->off_aircraftID)
		   sprintf(off_acID_str,"%d", trlrqtPtr->off_aircraftID);
		 if(trlrqtPtr->on_aircraftID)
	       sprintf(on_acID_str,"%d", trlrqtPtr->on_aircraftID);
		 if(trlrqtPtr->tixquestid_cancelled)
		   sprintf(requestid_cancelled_str, "%d", trlrqtPtr->tixquestid_cancelled);
		 fprintf(logFile,"| %-12d | %-12d | %10s | %-12d | %10s | %-15d | %10s | %11s | %-11d | %-11d | %-16s | %-11d |\n",
				trlrqtPtr->crewID,
				trlrqtPtr->dept_aptID_travel,
				dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"),
				trlrqtPtr->arr_aptID_travel,
				dt_DateTimeToDateTimeString(dt2, opbuf2, "%Y/%m/%d %H:%M"),
				trlrqtPtr->flight_purpose,
				trlrqtPtr->off_aircraftID? off_acID_str: "null",
				trlrqtPtr->on_aircraftID? on_acID_str: "null",
				trlrqtPtr->buyticket? 1:0,
				trlrqtPtr->cancelexstticket? 1:0,
				trlrqtPtr->tixquestid_cancelled? requestid_cancelled_str : "null",
				trlrqtPtr->groundtravel); //,scenarioid);
		 
		 sprintf(sqlBuf, "insert into proposed_travelrequest values(null,%d,%d,'%s',%d,'%s',%d, %s, %s,%d,%d, %s, %d, %d)",
			    trlrqtPtr->crewID,
				trlrqtPtr->dept_aptID_travel,
				dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"),
				trlrqtPtr->arr_aptID_travel,
				dt_DateTimeToDateTimeString(dt2, opbuf2, "%Y/%m/%d %H:%M"),
				trlrqtPtr->flight_purpose,
				trlrqtPtr->off_aircraftID? off_acID_str: "null",
				trlrqtPtr->on_aircraftID? on_acID_str: "null",
                trlrqtPtr->buyticket? 1:0,
				trlrqtPtr->cancelexstticket? 1:0,
				trlrqtPtr->tixquestid_cancelled? requestid_cancelled_str : "null",
                trlrqtPtr->groundtravel,
				scenarioid);
		 
		if(!(myDoWrite(conn,sqlBuf))) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
			writeWarningData(myconn); exit(1);
		}
		free(trlrqtPtr);
      
	  }
	}
    fprintf(logFile,"+--------------+--------------+------------------+--------------+------------------+-----------------+------------+-------------+-------------+-------------+------------------+----------------+\n");
 
	sprintf(sqlBuf,"update optimizer_results set run_end = now(), exit_status = 0, exit_message = 'normal exit.' where local_scenarioid = %d",
		scenarioid);
	if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	//Stats Collection - 02/12/08 ANG
	if(optParam.runOptStats == 1) runOptimizerStatistics(conn, scenarioid);
	if(optParam.runOptStatsByDay == 1 && optParam.planningWindowDuration > 1) runOptimizerStatisticsByDay(conn, scenarioid); 
	if(optParam.updateforSimu == 1){ 
		//test
		//printf("Do you want to promote or not?\n");
		//scanf("%d",&(optParam.updateforSimu)); 
		//if(optParam.updateforSimu)
		//test
		   writeDataforSimu(conn, scenarioid);
	}
}

/********************************************************************************
*	Function:  runOptimizerStatistics - 02/12/08 ANG
*	Purpose:  collect statistics from optimizer run
********************************************************************************/
void
runOptimizerStatistics(MY_CONNECTION *conn, int scenarioid)
{
//	int errNbr1; 
//	int i;
	char sqlBuf[102400];
	char sqlBuf1[102400];
	char sqlBuf2[102400];
	char sqlBuf3[102400];
//	char tbuf1[32], tbuf2[32];
//	DateTime dt1, dt2;
	extern char *username;
	extern int verbose; 
	extern char *statSQL;
	extern char *statSchedSQL;  // 11/10/08 ANG
	extern char *statSchedSQL2;  // 11/10/08 ANG
	extern char *statSchedSQL3;  // 11/10/08 ANG
	extern char *macStatSQL; // 11/17/08 ANG
	extern char *macStatSQL2; // 11/17/08 ANG

	if(! conn) {
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

	//if(verbose)
	//	logMsg(logFile,"statSQL:\n%s\n\n", statSQL);

	sprintf(sqlBuf, statSQL, username, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid);
		//dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
		//	asctime(gmtime(&(optParam.windowStart))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M"), 
		//	username, scenarioid);

	if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(verbose)
		logMsg(logFile,"** Optimizer statistics stored.\n");

	//Running stats collection for SCHED - 11/10/08 ANG

	sprintf(sqlBuf1, statSchedSQL, username, scenarioid);
	sprintf(sqlBuf2, statSchedSQL2);
	sprintf(sqlBuf3, statSchedSQL3, scenarioid);
	sprintf(sqlBuf, strcat(sqlBuf1, strcat(sqlBuf2, sqlBuf3)));

	//logMsg(logFile,"statSchedSQL = %s \n", sqlBuf);
	
	if(!(myDoWrite(conn, sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	if(verbose)
		logMsg(logFile,"** Schedule statistics stored.\n");

	//Run macStatSQL - 11/17/08 ANG
	sprintf(sqlBuf1, macStatSQL, username, scenarioid);
	sprintf(sqlBuf2, macStatSQL2, scenarioid, scenarioid, scenarioid, scenarioid);
	sprintf(sqlBuf, strcat(sqlBuf1, sqlBuf2));

	//sprintf(sqlBuf, macStatSQL, username, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid);

	//logMsg(logFile,"macStatSQL = %s \n", sqlBuf);

	if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(verbose)
		logMsg(logFile,"** Mac statistics stored.\n");

}

/********************************************************************************
*	Function:  runOptimizerStatisticsByDay - 12/05/08 ANG
*	Purpose:  collect statistics from optimizer run, separate by day
********************************************************************************/
void
runOptimizerStatisticsByDay(MY_CONNECTION *conn, int scenarioid)
{
	int sc;
	char sqlBuf[102400];
	char sqlBuf1[102400];
	char sqlBuf2[102400];
	char sqlBuf3[102400];
	char tbuf1[32], tbuf2[32];
	extern char *username;
	extern int verbose; 
	extern char *statSQLD1; 
	extern char *statSQLD2; 
	extern char *statSQLD3; 
	//extern char *statSchedSQLD1;  
	//extern char *statSchedSQLD2;  
	//extern char *statSchedSQLD3;  
	//extern char *statSchedSQL2D1;  
	//extern char *statSchedSQL2D2;  
	//extern char *statSchedSQL2D3;  
	//extern char *statSchedSQL3D1;  
	//extern char *statSchedSQL3D2;  
	//extern char *statSchedSQL3D3;  
	extern char *macStatSQLD1; 
	extern char *macStatSQLD2; 
	extern char *macStatSQLD3; 
	extern char *macStatSQL2D1; 
	extern char *macStatSQL2D2; 
	extern char *macStatSQL2D3; 
	time_t datecut1, datecut2, datecut3; 
	char *s1;

	if(! conn) {
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

	if ((optParam.planningWindowDuration == 3 && dt_addToDateTime(Hours, -2*24, dt_time_tToDateTime(optParam.windowEnd)) > dt_time_tToDateTime(optParam.windowStart)) ||
		(optParam.planningWindowDuration == 2 && dt_addToDateTime(Hours, -1*24, dt_time_tToDateTime(optParam.windowEnd)) > dt_time_tToDateTime(optParam.windowStart))){
		if(optParam.planningWindowDuration == 3){
			datecut1 = DateTimeToTime_t(dt_addToDateTime(Hours, -2*24, dt_time_tToDateTime(optParam.windowEnd)));
			datecut2 = DateTimeToTime_t(dt_addToDateTime(Hours, -1*24, dt_time_tToDateTime(optParam.windowEnd)));
			datecut3 = optParam.windowEnd;
		}
		else if (optParam.planningWindowDuration == 2){
			datecut1 = DateTimeToTime_t(dt_addToDateTime(Hours, -1*24, dt_time_tToDateTime(optParam.windowEnd)));
			datecut2 = optParam.windowEnd;
			datecut3 = optParam.windowEnd;
		}

		s1 = substitute(statSQLD1, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(optParam.windowStart), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSQLD1 = strdup(s1);
		s1 = substitute(statSQLD1, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSQLD1 = strdup(s1);
		s1 = substitute(statSQLD1, "'%s' username", "'%s-D1' username", &sc);
		statSQLD1 = strdup(s1);

		s1 = substitute(statSQLD2, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSQLD2 = strdup(s1);
		s1 = substitute(statSQLD2, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSQLD2 = strdup(s1);
		s1 = substitute(statSQLD2, "'%s' username", "'%s-D2' username", &sc);
		statSQLD2 = strdup(s1);

		s1 = substitute(statSQLD3, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSQLD3 = strdup(s1);
		s1 = substitute(statSQLD3, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut3), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSQLD3 = strdup(s1);
		s1 = substitute(statSQLD3, "'%s' username", "'%s-D3' username", &sc);
		statSQLD3 = strdup(s1);

		//s1 = substitute(statSchedSQLD1, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(optParam.windowStart), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQLD1 = strdup(s1);
		//s1 = substitute(statSchedSQLD1, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQLD1 = strdup(s1);

		//s1 = substitute(statSchedSQLD2, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQLD2 = strdup(s1);
		//s1 = substitute(statSchedSQLD2, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQLD2 = strdup(s1);

		//s1 = substitute(statSchedSQLD3, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQLD3 = strdup(s1);
		//s1 = substitute(statSchedSQLD3, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut3), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQLD3 = strdup(s1);

		//s1 = substitute(statSchedSQL2D1, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(optParam.windowStart), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL2D1 = strdup(s1);
		//s1 = substitute(statSchedSQL2D1, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL2D1 = strdup(s1);

		//s1 = substitute(statSchedSQL2D2, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL2D2 = strdup(s1);
		//s1 = substitute(statSchedSQL2D2, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL2D2 = strdup(s1);

		//s1 = substitute(statSchedSQL2D3, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL2D3 = strdup(s1);
		//s1 = substitute(statSchedSQL2D3, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut3), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL2D3 = strdup(s1);

		//s1 = substitute(statSchedSQL3D1, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(optParam.windowStart), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL3D1 = strdup(s1);
		//s1 = substitute(statSchedSQL3D1, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL3D1 = strdup(s1);

		//s1 = substitute(statSchedSQL3D2, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL3D2 = strdup(s1);
		//s1 = substitute(statSchedSQL3D2, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL3D2 = strdup(s1);

		//s1 = substitute(statSchedSQL3D3, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL3D3 = strdup(s1);
		//s1 = substitute(statSchedSQL3D3, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut3), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		//statSchedSQL3D3 = strdup(s1);

		s1 = substitute(macStatSQLD1, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(optParam.windowStart), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQLD1 = strdup(s1);
		s1 = substitute(macStatSQLD1, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQLD1 = strdup(s1);
		s1 = substitute(macStatSQLD1, "-MAC", "-MAC-D1", &sc);
		macStatSQLD1 = strdup(s1);

		s1 = substitute(macStatSQLD2, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQLD2 = strdup(s1);
		s1 = substitute(macStatSQLD2, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQLD2 = strdup(s1);
		s1 = substitute(macStatSQLD2, "-MAC", "-MAC-D2", &sc);
		macStatSQLD2 = strdup(s1);

		s1 = substitute(macStatSQLD3, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQLD3 = strdup(s1);
		s1 = substitute(macStatSQLD3, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut3), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQLD3 = strdup(s1);
		s1 = substitute(macStatSQLD3, "-MAC", "-MAC-D3", &sc);
		macStatSQLD3 = strdup(s1);

		s1 = substitute(macStatSQL2D1, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(optParam.windowStart), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQL2D1 = strdup(s1);
		s1 = substitute(macStatSQL2D1, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQL2D1 = strdup(s1);

		s1 = substitute(macStatSQL2D2, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQL2D2 = strdup(s1);
		s1 = substitute(macStatSQL2D2, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQL2D2 = strdup(s1);

		s1 = substitute(macStatSQL2D3, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQL2D3 = strdup(s1);
		s1 = substitute(macStatSQL2D3, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut3), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		macStatSQL2D3 = strdup(s1);
	}

	//if(verbose){
	//	logMsg(logFile,"statSQLD1:\n%s\n\n", statSQLD1);
	//	logMsg(logFile,"statSQLD2:\n%s\n\n", statSQLD2);
	//	logMsg(logFile,"statSQLD3:\n%s\n\n", statSQLD3);
	//}

	//Running OPT-Day-1
	sprintf(sqlBuf, statSQLD1, username, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid);
	//logMsg(logFile,"statSQLD1:\n%s\n\n", sqlBuf);
	if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	if(verbose)
		logMsg(logFile,"** Optimizer statistics for Day-1 stored.\n");

	//Running OPT-Day-2
	sprintf(sqlBuf, statSQLD2, username, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid);
	//logMsg(logFile,"statSQLD2:\n%s\n\n", sqlBuf);
	if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	if(verbose)
		logMsg(logFile,"** Optimizer statistics for Day-2 stored.\n");

	//Running OPT-Day-3
	if(optParam.planningWindowDuration == 3){
		sprintf(sqlBuf, statSQLD3, username, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid, scenarioid);
		//logMsg(logFile,"statSQLD3:\n%s\n\n", sqlBuf);
		if(!(myDoWrite(conn,sqlBuf))) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
			writeWarningData(myconn); exit(1);
		}
		if(verbose)
			logMsg(logFile,"** Optimizer statistics for Day-3 stored.\n");
	}

	////Running stats collection for SCHED-Day-1 - 12/08/08 ANG
	//sprintf(sqlBuf1, statSchedSQLD1, username, scenarioid);
	//sprintf(sqlBuf2, statSchedSQL2D1);
	//sprintf(sqlBuf3, statSchedSQL3D1);
	//sprintf(sqlBuf, strcat(sqlBuf1, strcat(sqlBuf2, sqlBuf3)));
	////logMsg(logFile,"statSchedSQLD1 = %s \n", sqlBuf);
	//if(!(myDoWrite(conn, sqlBuf))) {
	//	logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
	//	writeWarningData(myconn); exit(1);
	//}
	//if(verbose)
	//	logMsg(logFile,"** Schedule statistics for Day-1 stored.\n");

	////Running stats collection for SCHED-Day-2 - 12/08/08 ANG
	//sprintf(sqlBuf1, statSchedSQLD2, username, scenarioid);
	//sprintf(sqlBuf2, statSchedSQL2D2);
	//sprintf(sqlBuf3, statSchedSQL3D2);
	//sprintf(sqlBuf, strcat(sqlBuf1, strcat(sqlBuf2, sqlBuf3)));
	//logMsg(logFile,"statSchedSQLD2 = %s \n", sqlBuf);
	//if(!(myDoWrite(conn, sqlBuf))) {
	//	logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
	//	writeWarningData(myconn); exit(1);
	//}
	//if(verbose)
	//	logMsg(logFile,"** Schedule statistics for Day-2 stored.\n");

	////Running stats collection for SCHED-Day-3 - 12/08/08 ANG
	//sprintf(sqlBuf1, statSchedSQLD3, username, scenarioid);
	//sprintf(sqlBuf2, statSchedSQL2D3);
	//sprintf(sqlBuf3, statSchedSQL3D3);
	//sprintf(sqlBuf, strcat(sqlBuf1, strcat(sqlBuf2, sqlBuf3)));
	////logMsg(logFile,"statSchedSQLD3 = %s \n", sqlBuf);
	//if(!(myDoWrite(conn, sqlBuf))) {
	//	logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
	//	writeWarningData(myconn); exit(1);
	//}
	//if(verbose)
	//	logMsg(logFile,"** Schedule statistics for Day-3 stored.\n");

	//Run macStatSQL Mac-Day-1 - 12/08/08 ANG
	sprintf(sqlBuf1, macStatSQLD1, username, scenarioid);
	sprintf(sqlBuf2, macStatSQL2D1, scenarioid, scenarioid, scenarioid, scenarioid);
	sprintf(sqlBuf, strcat(sqlBuf1, sqlBuf2));
	//logMsg(logFile,"macStatSQLD1 = %s \n", sqlBuf);
	if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	if(verbose)
		logMsg(logFile,"** Mac statistics for Day-1 stored.\n");

	//Run macStatSQL Mac-Day-2 - 12/08/08 ANG
	sprintf(sqlBuf1, macStatSQLD2, username, scenarioid);
	sprintf(sqlBuf2, macStatSQL2D2, scenarioid, scenarioid, scenarioid, scenarioid);
	sprintf(sqlBuf, strcat(sqlBuf1, sqlBuf2));
	//logMsg(logFile,"macStatSQLD2 = %s \n", sqlBuf);
	if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	if(verbose)
		logMsg(logFile,"** Mac statistics for Day-2 stored.\n");

	//Run macStatSQL Mac-Day-3 - 12/08/08 ANG
	if(optParam.planningWindowDuration == 3){
		sprintf(sqlBuf1, macStatSQLD3, username, scenarioid);
		sprintf(sqlBuf2, macStatSQL2D3, scenarioid, scenarioid, scenarioid, scenarioid);
		sprintf(sqlBuf, strcat(sqlBuf1, sqlBuf2));
		//logMsg(logFile,"macStatSQL = %s \n", sqlBuf);
		if(!(myDoWrite(conn,sqlBuf))) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
			writeWarningData(myconn); exit(1);
		}
		if(verbose)
			logMsg(logFile,"** Mac statistics for Day-3 stored.\n");
	}
	
}

/********************************************************************************
*	Function:  runOptimizerStatisticsByDay - 12/05/08 ANG
*	Purpose:  collect statistics from optimizer run, separate by day
********************************************************************************/
void
runScheduleStatisticsByDay(MY_CONNECTION *conn, int scenarioid)
{
	int sc;
	char sqlBuf[102400];
	char sqlBuf1[102400];
	char sqlBuf2[102400];
	char sqlBuf3[102400];
	char tbuf1[32], tbuf2[32];
	extern char *username;
	extern int verbose; 
	extern char *statSchedSQLD1;  
	extern char *statSchedSQLD2;  
	extern char *statSchedSQLD3;  
	extern char *statSchedSQL2D1;  
	extern char *statSchedSQL2D2;  
	extern char *statSchedSQL2D3;  
	extern char *statSchedSQL3D1;  
	extern char *statSchedSQL3D2;  
	extern char *statSchedSQL3D3;  
	time_t datecut1, datecut2, datecut3; 
	char *s1;

	if(! conn) {
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

	if ((optParam.planningWindowDuration == 3 && dt_addToDateTime(Hours, -2*24, dt_time_tToDateTime(optParam.windowEnd)) > dt_time_tToDateTime(optParam.windowStart)) || 
	    (optParam.planningWindowDuration == 2 && dt_addToDateTime(Hours, -1*24, dt_time_tToDateTime(optParam.windowEnd)) > dt_time_tToDateTime(optParam.windowStart))){
		if(optParam.planningWindowDuration == 3){
			datecut1 = DateTimeToTime_t(dt_addToDateTime(Hours, -2*24, dt_time_tToDateTime(optParam.windowEnd)));
			datecut2 = DateTimeToTime_t(dt_addToDateTime(Hours, -1*24, dt_time_tToDateTime(optParam.windowEnd)));
			datecut3 = optParam.windowEnd;
		}
		else if(optParam.planningWindowDuration == 2){
			datecut1 = DateTimeToTime_t(dt_addToDateTime(Hours, -1*24, dt_time_tToDateTime(optParam.windowEnd)));
			datecut2 = optParam.windowEnd;
			datecut3 = optParam.windowEnd;
		}

		s1 = substitute(statSchedSQLD1, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(optParam.windowStart), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQLD1 = strdup(s1);
		s1 = substitute(statSchedSQLD1, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQLD1 = strdup(s1);
		s1 = substitute(statSchedSQLD1, "-SCHED", "-SCHED-D1", &sc);
		statSchedSQLD1 = strdup(s1);

		s1 = substitute(statSchedSQLD2, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQLD2 = strdup(s1);
		s1 = substitute(statSchedSQLD2, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQLD2 = strdup(s1);
		s1 = substitute(statSchedSQLD2, "-SCHED", "-SCHED-D2", &sc);
		statSchedSQLD2 = strdup(s1);

		s1 = substitute(statSchedSQLD3, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQLD3 = strdup(s1);
		s1 = substitute(statSchedSQLD3, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut3), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQLD3 = strdup(s1);
		s1 = substitute(statSchedSQLD3, "-SCHED", "-SCHED-D3", &sc);
		statSchedSQLD3 = strdup(s1);

		s1 = substitute(statSchedSQL2D1, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(optParam.windowStart), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL2D1 = strdup(s1);
		s1 = substitute(statSchedSQL2D1, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL2D1 = strdup(s1);

		s1 = substitute(statSchedSQL2D2, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL2D2 = strdup(s1);
		s1 = substitute(statSchedSQL2D2, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL2D2 = strdup(s1);

		s1 = substitute(statSchedSQL2D3, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL2D3 = strdup(s1);
		s1 = substitute(statSchedSQL2D3, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut3), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL2D3 = strdup(s1);

		s1 = substitute(statSchedSQL3D1, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(optParam.windowStart), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL3D1 = strdup(s1);
		s1 = substitute(statSchedSQL3D1, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL3D1 = strdup(s1);

		s1 = substitute(statSchedSQL3D2, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut1), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL3D2 = strdup(s1);
		s1 = substitute(statSchedSQL3D2, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL3D2 = strdup(s1);

		s1 = substitute(statSchedSQL3D3, "${windowStart}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut2), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL3D3 = strdup(s1);
		s1 = substitute(statSchedSQL3D3, "${windowEnd}", dt_DateTimeToDateTimeString(dt_time_tToDateTime(datecut3), tbuf1, "%Y/%m/%d %H:%M"), &sc);
		statSchedSQL3D3 = strdup(s1);
	}

	//Running stats collection for SCHED-Day-1 - 12/08/08 ANG
	sprintf(sqlBuf1, statSchedSQLD1, username, scenarioid);
	sprintf(sqlBuf2, statSchedSQL2D1);
	sprintf(sqlBuf3, statSchedSQL3D1, scenarioid);
	sprintf(sqlBuf, strcat(sqlBuf1, strcat(sqlBuf2, sqlBuf3)));
	//logMsg(logFile,"statSchedSQLD1 = %s \n", sqlBuf);
	if(!(myDoWrite(conn, sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	if(verbose)
		logMsg(logFile,"** Schedule statistics for Day-1 stored.\n");

	//Running stats collection for SCHED-Day-2 - 12/08/08 ANG
	sprintf(sqlBuf1, statSchedSQLD2, username, scenarioid);
	sprintf(sqlBuf2, statSchedSQL2D2);
	sprintf(sqlBuf3, statSchedSQL3D2, scenarioid);
	sprintf(sqlBuf, strcat(sqlBuf1, strcat(sqlBuf2, sqlBuf3)));
	//logMsg(logFile,"statSchedSQLD2 = %s \n", sqlBuf);
	if(!(myDoWrite(conn, sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	if(verbose)
		logMsg(logFile,"** Schedule statistics for Day-2 stored.\n");

	//Running stats collection for SCHED-Day-3 - 12/08/08 ANG
	if(optParam.planningWindowDuration == 3){
		sprintf(sqlBuf1, statSchedSQLD3, username, scenarioid);
		sprintf(sqlBuf2, statSchedSQL2D3);
		sprintf(sqlBuf3, statSchedSQL3D3, scenarioid);
		sprintf(sqlBuf, strcat(sqlBuf1, strcat(sqlBuf2, sqlBuf3)));
		//logMsg(logFile,"statSchedSQLD3 = %s \n", sqlBuf);
		if(!(myDoWrite(conn, sqlBuf))) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
			writeWarningData(myconn); exit(1);
		}
		if(verbose)
			logMsg(logFile,"** Schedule statistics for Day-3 stored.\n");
	}
}

/********************************************************************************
*	Function:  initializeOutputData
*	Purpose:  create record in optimizer_results table
********************************************************************************/
void
initializeOutputData(int updateRemote)
{
	int errNbr1, errNbr2;
	char tbuf1[32], tbuf2[32];
	char sqlBuf[1024];
	struct hostent *hostinfo = NULL;
	char host_name[128];
	int retval;
	WSADATA wsaData;
	struct sockaddr_in addr;
	extern char *username;

	strcpy(ip_addr,"unknown_ip_addr");

	if ((retval = WSAStartup(0x202,&wsaData)) != 0) {
		logMsg(logFile,"%s Line %d: WSAStartup() failed with error: %d\n", __FILE__,__LINE__, WSAGetLastError());
        	WSACleanup();
		writeWarningData(myconn); exit(1);
	}

	if(! username || ! strlen(username)) {
		username = getenv("USERNAME");
		if(! username)
			username = "user_name?";
	}

    if(optParam.runType ==1)
		username ="hourlyupdate";  
	if(optParam.runType ==2)
        username ="nightlyprocess";

	retval = gethostname(host_name,127);
	if(retval != 0) {
		logMsg(logFile,"%s Line %d: couldn't get host name.\n", __FILE__,__LINE__);
		strcpy(host_name,"unknown_host");
	}
	else {
		hostinfo = gethostbyname(host_name);
		if (WSAGetLastError() != 0) {
			if (WSAGetLastError() == 11001)
				logMsg(logFile,"%s Line %d: gethostbyname(%s) failed with error: %d, host %s not found.\n",
					__FILE__,__LINE__, host_name, WSAGetLastError(), host_name);
			else
				logMsg(logFile,"%s Line %d: gethostbyname(%s) failed with error: %d.\n", __FILE__,__LINE__, host_name, WSAGetLastError());
			strcpy(host_name,"unknown_host");
		}
		else {
			strcpy(host_name, hostinfo->h_name);
			memset(&addr, '\0', sizeof(addr));
			addr.sin_addr = *(struct in_addr *) hostinfo->h_addr;
			strcpy(ip_addr, inet_ntoa(addr.sin_addr));
		}
	}


	//make sure we are Connected to MySQL db
	if(! myconn) {
		logMsg(logFile,"%s Line %d: no MySQL connection!\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

/*
local_scenarioid int(11) PRIMARY KEY NOT NULL auto_increment,
remote_scenarioid int(11),
hostname varchar(64),
username varchar(64),
ip_addr varchar(16),
run_start DATETIME NOT NULL,
run_end DATETIME,
planning_window_start DATETIME NOT NULL,
planning_window_end DATETIME NOT NULL,
exit_status int(11),
exit_message text,
*/
	// update local machine
	sprintf(sqlBuf,"insert into optimizer_results values(NULL,NULL,'%s','%s','%s',now(),NULL,'%s','%s',NULL,NULL)",
		host_name,
		username,
		ip_addr,
		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
			asctime(gmtime(&(optParam.windowStart))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M"),
		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
			asctime(gmtime(&(optParam.windowEnd))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M"));

	if(!(myDoWrite(myconn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	local_scenarioid = (int) mysql_insert_id(myconn->mysock);
	logMsg(logFile,"%s Line %d: local_scenarioid: %d\n", __FILE__,__LINE__,local_scenarioid);

	if(! updateRemote)
		return;

	// update remote machine
	sprintf(sqlBuf,"insert into optimizer_results values(NULL,%d,'%s','%s','%s',now(),NULL,'%s','%s',NULL,NULL)",
		local_scenarioid, /* remote on server */
		host_name,
		username,
		ip_addr,
		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
			asctime(gmtime(&(optParam.windowStart))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M"),
		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
			asctime(gmtime(&(optParam.windowEnd))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M"));

	if(!(myDoWrite(remotemyconn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,remotemyconn->my_errno, remotemyconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
	remote_scenarioid = (int) mysql_insert_id(remotemyconn->mysock);
	logMsg(logFile,"%s Line %d: remote_scenarioid: %d\n", __FILE__,__LINE__,remote_scenarioid);

	// update local machine with remote_scenarioid
	sprintf(sqlBuf,"update optimizer_results set remote_scenarioid = %d where local_scenarioid = %d",
		remote_scenarioid,
		local_scenarioid);
	if(!(myDoWrite(myconn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
}
/********************************************************************************
*	Function:  writeWarningData
*	Purpose:  write out warning and error info to db
********************************************************************************/
void
writeWarningData(MY_CONNECTION *conn) //int scenarioid)
{
  char writetodbstring2[1024];
  char local_scenarioidbuf[32];
  char aircraftidbuf[32];
  char crewidbuf[32];
  char airportidbuf[32];
  char demandidbuf[32];
  char crewassgidbuf[32];
  char crewpairidbuf[32]; 
  char actypeidbuf[32];
  char contractidbuf[32];
  char minutesbuf[32];
  char leg1idbuf[32];
  char leg2idbuf[32];
  char legindxbuf[32];
  char acidxbuf[32];
  char crewpairindxbuf[32];
  char maintindxbuf[32];
  char exclusionindexbuf[32];
  char format_numberbuf[32];
  Warning_error_Entry *warningPtr;
  int i;
  char *innotes;
  char outnotes1[200];
  char outnotes2[200];
 
  if(! conn) {
	    logMsg(logFile,"%s Line %d: no MySQL connection!\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
  }
  warningPtr=errorinfoList;
  for(i=0; warningPtr && i<errorNumber; warningPtr++,i++)
  {  if(warningPtr->local_scenarioid)
       sprintf(local_scenarioidbuf, "%d", warningPtr->local_scenarioid);
     if(warningPtr->aircraftid)
	   sprintf(aircraftidbuf, "%d", warningPtr->aircraftid);
	 if(warningPtr->crewid)
       sprintf(crewidbuf, "%d", warningPtr->crewid);
	 if(warningPtr->airportid)
       sprintf(airportidbuf, "%d", warningPtr->airportid);
	 if(warningPtr->demandid)
	   sprintf(demandidbuf, "%d", warningPtr->demandid);
	 if(warningPtr->crewassgid)
       sprintf(crewassgidbuf, "%d", warningPtr->crewassgid);
	 if(warningPtr->crewpairid)
	   sprintf(crewpairidbuf, "%d", warningPtr->crewpairid);
	 if(warningPtr->actypeid)
	   sprintf(actypeidbuf, "%d", warningPtr->actypeid);
	 if(warningPtr->contractid)
	   sprintf(contractidbuf, "%d", warningPtr->contractid);
	 if(warningPtr->minutes)
	   sprintf(minutesbuf, "%d", warningPtr->minutes);
	 if(warningPtr->leg1id)
	   sprintf(leg1idbuf, "%d", warningPtr->leg1id);
	 if(warningPtr->leg2id)
	   sprintf(leg2idbuf, "%d", warningPtr->leg2id);
	 if(warningPtr->legindx)
	   sprintf(legindxbuf, "%d", warningPtr->legindx);
	 if(warningPtr->acidx)
	   sprintf(acidxbuf, "%d", warningPtr->acidx);
	 if(warningPtr->crewpairindx)
	   sprintf(crewpairindxbuf, "%d", warningPtr->crewpairindx);
	 if(warningPtr->maintindx)
	   sprintf(maintindxbuf, "%d", warningPtr->maintindx);
	 if(warningPtr->exclusionindex)
	   sprintf(exclusionindexbuf, "%d", warningPtr->exclusionindex);
	 if(warningPtr->format_number)
	   sprintf(format_numberbuf, "%d", warningPtr->format_number);
	   innotes=warningPtr->notes;
	   escapeQuotes(innotes, outnotes2);
	   innotes=warningPtr->filename;
	   escapeQuotes(innotes, outnotes1);
	   sprintf(writetodbstring2, 
		    "insert into optimizer_status values('%s','%s','%s','%s','%s','%s','%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s','%s','%s')",
		                               warningPtr->local_scenarioid? local_scenarioidbuf : "null",
									   warningPtr->group_name?  warningPtr->group_name : "null",
									   warningPtr->aircraftid? aircraftidbuf : "null",
									   warningPtr->crewid? crewidbuf :  "null",
									   warningPtr->datatime_str? warningPtr->datatime_str : "null",
	                                   warningPtr->airportid ?  airportidbuf : "null",
									   warningPtr->demandid? demandidbuf : "null",
									   warningPtr->crewassgid? crewassgidbuf : "null",
									   warningPtr->crewpairid? crewpairidbuf : "null",
									   warningPtr->actypeid? actypeidbuf : "null",
									   warningPtr->contractid? contractidbuf : "null",
	                                   warningPtr->minutes? minutesbuf : "null",
									   warningPtr->leg1id? leg1idbuf: "null",
	                                   warningPtr->leg2id? leg2idbuf: "null",
	                                   warningPtr->legindx? legindxbuf: "null",
									   warningPtr->acidx? acidxbuf : "null",
	                                   warningPtr->crewpairindx? crewpairindxbuf: "null",
	                                   warningPtr->maintindx? maintindxbuf: "null",
	                                   warningPtr->exclusionindex? exclusionindexbuf : "null",
									   strlen(warningPtr->filename)? outnotes1: "null",
	                                   strlen(warningPtr->line_number)? warningPtr->line_number: "null",
	                                   warningPtr->format_number? format_numberbuf : "null",
									   outnotes2? outnotes2: "null");
    /* logMsg(logFile, "%s,%s,%s,%s,%s,%s,%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s,%s,%s\n",
		                               warningPtr->local_scenarioid? local_scenarioidbuf : "null",
									   warningPtr->group_name?  warningPtr->group_name : "null",
									   warningPtr->aircraftid? aircraftidbuf : "null",
									   warningPtr->crewid? crewidbuf :  "null",
									   warningPtr->datatime_str? warningPtr->datatime_str : "null",
	                                   warningPtr->airportid ?  airportidbuf : "null",
									   warningPtr->demandid? demandidbuf : "null",
									   warningPtr->crewassgid? crewassgidbuf : "null",
									   warningPtr->crewpairid? crewpairidbuf : "null",
									   warningPtr->actypeid? actypeidbuf : "null",
									   warningPtr->contractid? contractidbuf : "null",
	                                   warningPtr->minutes? minutesbuf : "null",
									   warningPtr->leg1id? leg1idbuf: "null",
	                                   warningPtr->leg2id? leg2idbuf: "null",
	                                   warningPtr->legindx? legindxbuf: "null",
									   warningPtr->acidx? acidxbuf : "null",
	                                   warningPtr->crewpairindx? crewpairindxbuf: "null",
	                                   warningPtr->maintindx? maintindxbuf: "null",
	                                   warningPtr->exclusionindex? exclusionindexbuf : "null",
									   strlen(warningPtr->filename)? warningPtr->filename: "null",
	                                   strlen(warningPtr->line_number)? warningPtr->line_number: "null",
	                                   warningPtr->format_number? format_numberbuf : "null",
									   outnotes? outnotes: "null"); */
	 if(!(myDoWrite(conn,writetodbstring2))) 
	 {logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
	  exit(1);
	 }
  }
}


void 
updateResultsTableforOAG(MY_CONNECTION *conn, int scenarioid)
{ 
	char sqlBuf[1024];

	sprintf(sqlBuf,"update optimizer_results set run_end = now(), exit_status = 0, exit_message = 'normal exit.' where local_scenarioid = %d",
		scenarioid);
	if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}
}



/********************************************************************************
*	Function:  writeDataforSimu
*	Purpose:  write out optimization
********************************************************************************/
void
writeDataforSimu(MY_CONNECTION *conn, int scenarioid)
{
	char sqlBuf[1024], sqlBuf1[1024], sqlBuf2[1024], sqlBuf3[1024];

    strcpy(sqlBuf, "delete from managedleg where schedout >= (SELECT date_format(planning_window_start, '%Y/%m/%d %H:%i')");  
	sprintf(sqlBuf1," FROM optimizer_results where local_scenarioid = %d)", scenarioid);
	strcat(sqlBuf, sqlBuf1);
    if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

    strcpy(sqlBuf,"delete from unmanagedleg where scheduledout >= (SELECT date_format(planning_window_start, '%Y/%m/%d %H:%i')");  
	sprintf(sqlBuf1, " FROM optimizer_results where local_scenarioid = %d)", scenarioid);
	strcat(sqlBuf, sqlBuf1);
    if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	strcpy(sqlBuf,"delete from logpageleg where scheduledout >= (SELECT date_format(planning_window_start, '%Y/%m/%d %H:%i')");
	sprintf(sqlBuf1, " FROM optimizer_results where local_scenarioid = %d)", scenarioid);
	strcat(sqlBuf, sqlBuf1);
    if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

    //strcpy(sqlBuf,"delete from crewassignment where starttime >= (SELECT date_format(planning_window_start, '%Y/%m/%d %H:%i')");
	//sprintf(sqlBuf1, " FROM optimizer_results where local_scenarioid = %d)", scenarioid);
    strcpy(sqlBuf,"delete from crewassignment where endtime >= (SELECT date_format(planning_window_start, '%Y/%m/%d %H:%i')");
	sprintf(sqlBuf1,"FROM optimizer_results where local_scenarioid = %d) ", scenarioid, scenarioid);//RLZ removed crewid restriction, 06/10/09
	strcat(sqlBuf, sqlBuf1);
    if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

     strcpy(sqlBuf,"insert into managedleg (managedlegid, demandid, schedoutfboid, schedinfboid, schedoutaptid, schedout_icao, schedout_timezoneid, schedinaptid, schedin_icao, schedin_timezoneid, aircraftid, schedout, schedin, scenarioid, crewnotified, manuallyassigned)\
     select pmdg.managedlegid, pmdg.demandid, pmdg.schedoutfboid, pmdg.schedinfboid, pmdg.schedoutaptid, apt1.icao, apt1.timezoneid, pmdg.schedinaptid, apt2.icao, apt2.timezoneid,  pmdg.aircraftid, date_format(pmdg.schedout, '%Y/%m/%d %H:%i'), date_format(pmdg.schedin, '%Y/%m/%d %H:%i'), 4309, 0, 0");
	 sprintf(sqlBuf1, " from proposed_managedleg pmdg, airport apt1, airport apt2 where pmdg.schedoutaptid = apt1.airportid and pmdg.schedinaptid = apt2.airportid and scenarioid = %d", scenarioid);
     strcat(sqlBuf, sqlBuf1);
     if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	 }

     strcpy(sqlBuf,"insert into unmanagedleg (unmanagedlegid, demandid, departurefboid, arrivalfboid, departureairportid, outapt_icao, outapt_timezoneid, arrivalairportid, inapt_icao, inapt_timezoneid, aircraftid, scheduledout, scheduledin, scenarioid, manuallyassigned)\
     select pumdg.unmanagedlegid, pumdg.demandid, pumdg.departurefboid, pumdg.arrivalfboid, pumdg.departureairportid, apt1.icao, apt1.timezoneid, pumdg.arrivalairportid, apt2.icao, apt2.timezoneid, pumdg.aircraftid, date_format(pumdg.scheduledout, '%Y/%m/%d %H:%i'), date_format(pumdg.scheduledin, '%Y/%m/%d %H:%i'), 4309, 0");
	 sprintf(sqlBuf1, " from proposed_unmanagedleg pumdg, airport apt1, airport apt2 where pumdg.departureairportid = apt1.airportid and pumdg.arrivalairportid = apt2.airportid and scenarioid = %d", scenarioid);
	 strcat(sqlBuf, sqlBuf1);
	 if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	 }
     
	 //adjOverlapedCrewAssignments(conn, scenarioid, optParam.windowStart);
	 strcpy(sqlBuf,"insert into crewassignment  (crewassignmentid, crewid, aircraftid, position, starttime, endtime, scenarioid)\
     select crewassignmentid, crewid, aircraftid, position, date_format(starttime, '%Y/%m/%d %H:%i') as starttime, date_format(endtime, '%Y/%m/%d %H:%i'), 4309");
	 //sprintf(sqlBuf1, " from proposed_crewassignment where scenarioid = %d", scenarioid);
     sprintf(sqlBuf1, " from proposed_crewassignment where scenarioid = %d", scenarioid);
	 strcat(sqlBuf, sqlBuf1);
     //strcpy(sqlBuf2, " and starttime >= (SELECT date_format(planning_window_start, '%Y/%m/%d %H:%i')");
     //sprintf(sqlBuf3, " FROM optimizer_results where local_scenarioid = %d)", scenarioid);
	 //strcat(sqlBuf2, sqlBuf3);
     //strcat(sqlBuf, sqlBuf2);
     if(!(myDoWrite(conn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
     }
	 updateCsTravelFlights(conn, scenarioid);
	 writeProposedTravelFlights(conn, scenarioid);
}

typedef enum { travel_requestid = 0, travel_crewID, travel_dep_aptid, travel_earliest_dep, travel_arr_aptid,
   travel_latest_arr, travel_flight_purpose, travel_from_acID, travel_to_acID, travel_buyticket, travel_cancel_exist_ticket, travel_rqstid_cancelled, travel_groundtravl, travel_scenario
} TravelRequestColumns;

typedef enum
{ field_crewid=0, field_dpt_aptid, field_arr_aptid, field_travel_dpttm, field_travel_arrtm, field_rqtid
}CsTravelColmns;

/********************************************************************************
*	Function:  updateCsTravelFlights
*	Purpose:  update commercial flights into cs_travel_flights according to proposed_travelrequest
********************************************************************************/
void updateCsTravelFlights(MY_CONNECTION *conn, int scenarioid)
{ char deleteCtrlUnnece[1028], maxrequestidSQL[256], travel_insertSQL[1024], travel_updateSQL[1024], existing_travel_flightsSQL[2048];
  MYSQL_RES *res;
  MYSQL_FIELD *cols;
  my_ulonglong rowCount, rows;
  MYSQL_ROW row;
  int crewID;
  int request_dep_aptID, request_arr_aptID, maxrequestID, request_flight_purpose, request_groundtravl;
  char *request_earliest_dep, *request_latest_arr;
  char *paramVal;
  CsTravelData *cstrlPtr;
  int from_acID, to_acID;
  int errNbr,concurr_inserted = 0;
  int previous_crewid;
  int cancel_exist_ticket, tix_rqstid_cancelled;


  if(!(paramVal = getCmdLineVar("fakeRuntime"))) {
	  if(!(paramVal = getParamValue("fakeRuntime"))) {
		logMsg(logFile,"Required parameter \"fakeRuntime(runtime)\" missing from parameter file.\n");
		writeWarningData(myconn); exit(1);
	  }
	}
  sprintf(deleteCtrlUnnece, "delete csflt.* from cs_travel_flights csflt,\n\
                                      (select cstrlflight.traveller_id, cstrlflight.request_status, min(departure_gmt_time) as departure_time,\n\
									          max(arrival_gmt_time) as arrival_time, request_id\n\
									   from\n\
                                             cs_travel_flights cstrlflight\n\
                                       group by request_id) tmp\n\
                                where tmp.request_id = csflt.request_id and tmp.traveller_id = csflt.traveller_id and\n\
								      tmp.departure_time > date_format(date_add('%s', interval %d+ 4 hour), '%%Y/%%m/%%d %%H:%%i')",
		  paramVal, optParam.travelcutoff/2);

  if(!(myDoWrite(conn,deleteCtrlUnnece))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}


  if(!(myDoWrite(conn,"delete from cs_travel_flights where  departure_gmt_time is null or arrival_gmt_time is null"))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

  sprintf(existing_travel_flightsSQL, "select aa.traveller_id,  bb.departure_aptid, cc.arrival_aptid, aa.departure_time, aa.arrival_time, aa.request_id\n\
                                       from\n\
                                          (select cstrlflight.traveller_id, cstrlflight.request_status, min(departure_gmt_time) as departure_time,  max(arrival_gmt_time) as arrival_time, request_id\n\
								           from\n\
								                  cs_travel_flights cstrlflight where traveller_id in\n\
									          (SELECT distinct a.crewID\n\
                                               FROM proposed_travelrequest a,  cs_travel_flights b where a.scenarioid = %d and a.crewID = b.traveller_id and a.buyticket = 1)\n\
                                           group by request_id) aa, cs_travel_flights bb, cs_travel_flights cc\n\
                                       where aa.traveller_id = bb.traveller_id  and aa.request_id =bb.request_id\n\
                                            and aa.traveller_id = cc.traveller_id and aa.request_id = cc.request_id\n\
                                            and aa.departure_time = bb.departure_gmt_time and aa.arrival_time = cc.arrival_gmt_time", scenarioid);

    if(!myDoQuery(conn, existing_travel_flightsSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(myconn); exit(1);
	}

	if(! res) {
		logMsg(logFile,"No commercial travel information read for existing_travel_flightsSQL.\n");
		writeWarningData(myconn);
		exit(1);
	}

	rowCount = mysql_num_rows(res);
	if(! rowCount) {
		logMsg(logFile,"existing_travel_flightsSQL: no results.\n");
		return(0);
	}
	
	for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		if((cstrlPtr = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) 
	    {   logMsg(logFile,"%s Line %d, Out of Memory in updateCsTravelFlights().\n", __FILE__, __LINE__);
		    writeWarningData(myconn); exit(1);
	    }
		cstrlPtr->crewID = atoi(row[field_crewid]);
	    cstrlPtr->rqtID = atoi(row[field_rqtid]);
		cstrlPtr->dpt_aptID = atoi(row[field_dpt_aptid]);
		cstrlPtr->arr_aptID = atoi(row[field_arr_aptid]);
		if ((cstrlPtr->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[field_travel_dpttm], NULL, &errNbr)) == BadDateTime) {
	      logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[field_travel_dpttm]);
		  exit(1);
		}
		if ((cstrlPtr->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[field_travel_arrtm], NULL, &errNbr)) == BadDateTime) {
	      logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[field_travel_arrtm]);
		  exit(1);
		}
		if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr, travelFlightCmp))) {
		  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateCsTravelFlights().\n",__FILE__,__LINE__);
		  exit(1);
		}
        //test
		concurr_inserted++;
		//test
	}
  mysql_free_result(res);

  sprintf(maxrequestidSQL, "select max(request_id) from cs_travel_flights");
  if(!myDoQuery(conn, maxrequestidSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(conn); exit(1);
  }
  if(! res){
		logMsg(logFile,"updateCsTravelFlights(): no results.\n");
		writeWarningData(conn); exit(1);
  }
  rowCount = mysql_num_rows(res);
  if(! rowCount) {
		logMsg(logFile,"updateCsTravelFlights(): 0 rows returned for maxrequestidSQL.\n");
		writeWarningData(conn); exit(1);
  }
  for(rows = 0; rows < rowCount; rows++) {
	row = mysql_fetch_row(res);
	if(! row)
		break;
	maxrequestID =atoi(row[0]);
  }
  mysql_free_result(res);

  sprintf(travel_insertSQL, "select * from proposed_travelrequest where scenarioid = %d and crewid not in\n\
                        (SELECT distinct a.crewID\n\
                        FROM proposed_travelrequest a,  cs_travel_flights b where a.scenarioid = %d and a.crewID = b.traveller_id and a.buyticket = 1) and buyticket = 1\n\
						and earliest_dept <= date_format(date_add('%s', interval %d+ 4 hour), '%%Y/%%m/%%d %%H:%%i')",scenarioid, scenarioid, paramVal, optParam.travelcutoff/2);
  if(!myDoQuery(conn, travel_insertSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(conn); exit(1);
  }
  if(! res){
		logMsg(logFile,"updateCsTravelFlights(): no results.\n");
		writeWarningData(conn); exit(1);
  }
  rowCount = mysql_num_rows(res);
  if(! rowCount) {
		logMsg(logFile,"updateCsTravelFlights(): 0 rows returned.\n");
		writeWarningData(conn);
  }
  for(rows = 0; rows < rowCount; rows++) {
	row = mysql_fetch_row(res);
	if(! row)
		break;
	request_flight_purpose = atoi(row[travel_flight_purpose]);
	if(request_flight_purpose == 3){
		from_acID = row[travel_from_acID]? atoi(row[travel_from_acID]):0;
		to_acID = row[travel_to_acID]? atoi(row[travel_to_acID]):0;
		if(from_acID == to_acID)
		  continue;
	}
	crewID =atoi(row[travel_crewID]);
    request_dep_aptID = atoi(row[travel_dep_aptid]); 
    request_earliest_dep = strdup(row[travel_earliest_dep]);
    request_arr_aptID = atoi(row[travel_arr_aptid]);
    request_latest_arr = strdup(row[travel_latest_arr]);
	request_groundtravl = atoi(row[travel_groundtravl]); 
	cancel_exist_ticket = atoi(row[travel_cancel_exist_ticket]);
	if(cancel_exist_ticket)
        tix_rqstid_cancelled = atoi(row[travel_rqstid_cancelled]); 
	else
        tix_rqstid_cancelled = 0;
	generateOneTravelRecord(crewID, request_dep_aptID, request_arr_aptID, request_earliest_dep, request_latest_arr, request_flight_purpose, 0, request_groundtravl, tix_rqstid_cancelled, &maxrequestID);
  }
  mysql_free_result(res);

  sprintf(travel_updateSQL, "select * from proposed_travelrequest where scenarioid = %d and crewid in\n\
                        (SELECT distinct a.crewID\n\
                        FROM proposed_travelrequest a,  cs_travel_flights b where a.scenarioid = %d and a.crewID = b.traveller_id and a.buyticket = 1)\n\
						and earliest_dept <= date_format(date_add('%s', interval %d+ 4 hour), '%%Y/%%m/%%d %%H:%%i') and buyticket = 1 order by crewID, earliest_dept",
		  scenarioid, scenarioid, paramVal, optParam.travelcutoff/2);
  if(!myDoQuery(conn, travel_updateSQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
		writeWarningData(conn); exit(1);
  }
  if(! res){
		logMsg(logFile,"updateCsTravelFlights(): no results.\n");
		writeWarningData(conn); exit(1);
  }
  rowCount = mysql_num_rows(res);
  if(! rowCount) {
		logMsg(logFile,"updateCsTravelFlights(): 0 rows returned.\n");
		writeWarningData(conn);
  }
  previous_crewid = 0;
   for(rows = 0; rows < rowCount; rows++){
		row = mysql_fetch_row(res);
		if(! row)
			break;
		request_flight_purpose = atoi(row[travel_flight_purpose]);
		//if(request_flight_purpose == 3){
		//	from_acID = row[travel_from_acID]? atoi(row[travel_from_acID]):0;
		//	to_acID = row[travel_to_acID]? atoi(row[travel_to_acID]):0;
		//	if(from_acID == to_acID)
		//	  continue;
		//}
		crewID =atoi(row[travel_crewID]);
		if (crewID == previous_crewid)
			continue;
		request_dep_aptID = atoi(row[travel_dep_aptid]); 
		request_earliest_dep = strdup(row[travel_earliest_dep]);
		request_arr_aptID = atoi(row[travel_arr_aptid]);
		request_latest_arr = strdup(row[travel_latest_arr]);
		request_groundtravl = atoi(row[travel_groundtravl]);
		cancel_exist_ticket = atoi(row[travel_cancel_exist_ticket]);
		if(cancel_exist_ticket)
			tix_rqstid_cancelled = atoi(row[travel_rqstid_cancelled]); 
		else
			tix_rqstid_cancelled = 0;
		generateOneTravelRecord(crewID, request_dep_aptID, request_arr_aptID, request_earliest_dep, request_latest_arr, request_flight_purpose, 1, request_groundtravl, tix_rqstid_cancelled, &maxrequestID);
		previous_crewid = crewID;
   }

}

typedef enum { travel_flight_no = 0, travel_flight_carrier, travel_orgAptID, travel_dptTime,
    travel_destAptID, travel_arrTime
} OAG_Direct_Columns;
/*******************************************************************************
*	Function:  generateOneTravelRecord
*	Purpose:  Given a travel request record, get corresponding commerical 
*             flight with earliest arrival info from OAG and insert/update cs_travel_flight
********************************************************************************/
void generateOneTravelRecord(int crewID, int request_dep_aptID, int request_arr_aptID, char *request_earliest_dep, char *request_latest_arr, int request_flight_purpose, int updatetype, int request_groundtravl, int tix_rqstid_cancelled, int *maxrequestid)
{  char TravelRequest1ItinerarySQL[4096], strSQL1[4096], strSQL2[256], strSQL3[256], strSQL4[256];
   char oag1sql[2048], oag2sql[2048];
   char tmp_str1[256], tmp_str2[16];
   GeneratedCsTravel *gntcstrlPtr;
   MYSQL_RES *res;
   MYSQL_FIELD *cols;
   my_ulonglong rowCount, rows;
   TravelRequest *trlrqtPtr;
   int tmp_travellerid, tmp_dpt_aptid, tmp_arr_aptid, tmp_requestid;
   char *tmp_dpt_time, *tmp_arr_time;
   MYSQL_ROW row;
   int mapp_aptID, mapp_apt_commFlag;
   DateTime DateTime_dptTm, DateTime_arrTm;
   int errNbr;
   GeneratedCsTravel *gencstrlPtr;
   char tbuf1[32], tbuf2[32],tbuf3[32],tbuf4[32];
   CjoinResult *resultPtr;
   int cjoin_return;
   BINTREENODE *tmp;
   int onestopNbr, i =0, comm_mapp_apt_num = 0;
   DateTime tmp_dpt, tmp_arr, tmp_dt1, tmp_dt2;
   char *new_rqst_depTm, *new_rqst_arrTm;

   //test
   //optParam.groundTravel = 45;
   int groundTravelTm = 45;
   //test

   
   if(!request_groundtravl){
     if ((tmp_dpt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", request_earliest_dep, NULL, &errNbr)) == BadDateTime) {
      logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, request_earliest_dep);
	  exit(1); }
	 if ((tmp_arr = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", request_latest_arr, NULL, &errNbr)) == BadDateTime) {
      logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, request_latest_arr);
	  exit(1); }

	 tmp_dpt = dt_addToDateTime(Minutes, optParam.preBoardTime, tmp_dpt);
	 tmp_arr = dt_addToDateTime(Minutes, -optParam.postArrivalTime, tmp_arr);

	 new_rqst_depTm = strdup(dt_DateTimeToDateTimeString(tmp_dpt,tbuf1, "%Y/%m/%d %H:%M"));
     new_rqst_arrTm = strdup(dt_DateTimeToDateTimeString(tmp_arr,tbuf2, "%Y/%m/%d %H:%M"));

   sprintf(strSQL1, "\
select\n\
	FD_flight_number,\n\
	FD_airline_desig,\n\
	dep_aptid,\n\
	UTC_departure_time,\n\
	arr_aptid,\n\
	UTC_arrival_time\n\
from\n\
	oag.ssim ssim\n\
where\n\
	dep_aptid in\n");

   sprintf(tmp_str1,"(");
   while(i< aptList[request_dep_aptID].numMaps && aptList[request_dep_aptID].aptMapping[i].duration <=50){
       mapp_aptID = aptList[request_dep_aptID].aptMapping[i].airportID;
       mapp_apt_commFlag = aptList[mapp_aptID].commFlag;
	   if(aptList[request_dep_aptID].numMaps == 1){
	     sprintf(tmp_str2,"%d",aptList[request_dep_aptID].aptMapping[i].airportID);
         strcat(tmp_str1, tmp_str2);
		 comm_mapp_apt_num++;
	   }
	   else if(mapp_apt_commFlag){
	     if(comm_mapp_apt_num ==0)
		   sprintf(tmp_str2,"%d",aptList[request_dep_aptID].aptMapping[i].airportID);
		 else
	       sprintf(tmp_str2,",%d",aptList[request_dep_aptID].aptMapping[i].airportID);
		 strcat(tmp_str1, tmp_str2);
         comm_mapp_apt_num++;
		 if(comm_mapp_apt_num ==5)
		    break;
	   }
	   i++;
   }
   if(!comm_mapp_apt_num){
     if(aptList[request_dep_aptID].numMaps)
        sprintf(tmp_str2,"%d",aptList[request_dep_aptID].aptMapping[0].airportID);
	 else
	    sprintf(tmp_str2,"%d", request_dep_aptID);
     strcat(tmp_str1, tmp_str2);
   }
   strcat(tmp_str1,") and arr_aptid in\n");
   strcpy(strSQL2,tmp_str1);
   strcat(strSQL1, strSQL2);
  
   comm_mapp_apt_num = 0;
   i= 0;
   sprintf(tmp_str1,"(");
   while(i< aptList[request_arr_aptID].numMaps && aptList[request_arr_aptID].aptMapping[i].duration <=50){
       mapp_aptID = aptList[request_arr_aptID].aptMapping[i].airportID;
       mapp_apt_commFlag = aptList[mapp_aptID].commFlag;
	   if(aptList[request_arr_aptID].numMaps == 1){
	     sprintf(tmp_str2,"%d",aptList[request_arr_aptID].aptMapping[i].airportID);
         strcat(tmp_str1, tmp_str2);
         comm_mapp_apt_num++;
	   }
	   else if(mapp_apt_commFlag){
	     if(comm_mapp_apt_num ==0)
		   sprintf(tmp_str2,"%d",aptList[request_arr_aptID].aptMapping[i].airportID);
		 else
	       sprintf(tmp_str2,",%d",aptList[request_arr_aptID].aptMapping[i].airportID);
		 strcat(tmp_str1, tmp_str2);
         comm_mapp_apt_num++;
		 if(comm_mapp_apt_num ==5)
		    break;
	   }
	   i++;
   }
   if(!comm_mapp_apt_num){
      if(aptList[request_arr_aptID].numMaps)
         sprintf(tmp_str2,"%d",aptList[request_arr_aptID].aptMapping[0].airportID);
	  else 
		 sprintf(tmp_str2,"%d", request_arr_aptID);
      strcat(tmp_str1, tmp_str2);
   }
   strcat(tmp_str1,") and\n");
   strcpy(strSQL3,tmp_str1);
   strcat(strSQL1, strSQL3);
   
   sprintf(strSQL4," UTC_departure_time >='%s' and\n\
	UTC_arrival_time <= '%s'",
    new_rqst_depTm,
    new_rqst_arrTm );
    strcat(strSQL1, strSQL4);

	if(request_flight_purpose == 2)
			strcat(strSQL1,"order by UTC_arrival_time desc limit 6");
	else
            strcat(strSQL1,"order by UTC_arrival_time asc limit 6");

	strcpy(TravelRequest1ItinerarySQL,strSQL1);

  if(!myDoQuery(myconn, TravelRequest1ItinerarySQL, &res, &cols)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
  }
  if(! res){
		logMsg(logFile,"updateCsTravelFlights(): no results.\n");
		writeWarningData(myconn); exit(1);
  }
  rowCount = mysql_num_rows(res);
  //test
  if(!rowCount){
     //emptyRecNum++;
	 fprintf(logFile, "No Direct commercial travel available: Dep_AptID = %d, Arr_AptID = %d", request_dep_aptID, request_arr_aptID);
  }
  //
  if(! rowCount) {
		logMsg(logFile,"updateCsTravelFlights(): 0 rows returned for TravelRequest1ItinerarySQL.\n");
		writeWarningData(myconn);
  }


  //get non stop commercial itinerary
	  for(rows = 0; rows < rowCount; rows++) {
		row = mysql_fetch_row(res);
		if(! row)
			break;
		if((gencstrlPtr = (GeneratedCsTravel *) calloc((size_t) 1, sizeof(GeneratedCsTravel))) == NULL) {   
		  logMsg(logFile,"%s Line %d, Out of Memory in generateOneTravelRecord().\n", __FILE__, __LINE__);
		  writeWarningData(myconn); exit(1);
		}
		gencstrlPtr->travellerid = crewID;
		gencstrlPtr->leg1_dpt_aptid = atoi(row[travel_orgAptID]);
		gencstrlPtr->leg1_arr_aptid = atoi(row[travel_destAptID]);
		(*maxrequestid)++;
		gencstrlPtr->requestid = (*maxrequestid);
		gencstrlPtr->leg1_dpt_time = strdup(row[travel_dptTime]);
		gencstrlPtr->leg1_arr_time = strdup(row[travel_arrTime]);
		gencstrlPtr->directflight = 1;
		gencstrlPtr->flightpurpose = request_flight_purpose;
		gencstrlPtr->groundtravl = 0;
		gencstrlPtr->tix_rqstid_cancelled = tix_rqstid_cancelled;

		if(gencstrlPtr->leg1_dpt_aptid != request_dep_aptID)
		{  gencstrlPtr->groundtravel1 = 1;
		   gencstrlPtr->leg0_dpt_aptid = request_dep_aptID;
		   gencstrlPtr->leg0_arr_aptid = gencstrlPtr->leg1_dpt_aptid;
		   if ((tmp_arr = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg1_dpt_time, NULL, &errNbr)) == BadDateTime) {
			   logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg1_dpt_time);
			   exit(1);
			}
		   tmp_dt1 = dt_addToDateTime(Minutes, -optParam.preBoardTime, tmp_arr);
		   tmp_arr = tmp_dt1;
		   if ((tmp_dt2 = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", request_earliest_dep, NULL, &errNbr)) == BadDateTime) {
			   logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, request_earliest_dep);
			   exit(1);
			}
		   i= 0;
		   while(i< aptList[request_dep_aptID].numMaps){
			   if(aptList[request_dep_aptID].aptMapping[i].airportID == gencstrlPtr->leg1_dpt_aptid){
                    groundTravelTm = aptList[request_dep_aptID].aptMapping[i].duration;
					break;
			   }
			   i++;
		   }
		   tmp_dpt = max(tmp_dt2, dt_addToDateTime(Minutes, -groundTravelTm, tmp_dt1));
		   gencstrlPtr->leg0_dpt_time = strdup(dt_DateTimeToDateTimeString(tmp_dpt,tbuf1, "%Y/%m/%d %H:%M"));
		   gencstrlPtr->leg0_arr_time = strdup(dt_DateTimeToDateTimeString(tmp_arr,tbuf2, "%Y/%m/%d %H:%M"));
		   //gencstrlPtr->leg0_dpt_time =  strdup(request_earliest_dep);
		   //gencstrlPtr->leg0_arr_time =  strdup(gencstrlPtr->leg1_dpt_time);  
		}
		if(gencstrlPtr->leg1_arr_aptid != request_arr_aptID)
		{  gencstrlPtr->groundtravel2 = 1;
		   gencstrlPtr->leg3_dpt_aptid = gencstrlPtr->leg1_arr_aptid;
		   gencstrlPtr->leg3_arr_aptid = request_arr_aptID;
		   if ((tmp_dpt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg1_arr_time, NULL, &errNbr)) == BadDateTime) {
			   logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg1_arr_time);
			   exit(1);
			}
		   tmp_dt1 = dt_addToDateTime(Minutes, optParam.postArrivalTime, tmp_dpt);
		   tmp_dpt = tmp_dt1;
		   if ((tmp_dt2 = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", request_latest_arr, NULL, &errNbr)) == BadDateTime) {
			   logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, request_latest_arr);
			   exit(1);
			}
		    i= 0;
		   while(i< aptList[gencstrlPtr->leg1_arr_aptid].numMaps){
			   if(aptList[gencstrlPtr->leg1_arr_aptid].aptMapping[i].airportID == request_arr_aptID){
                    groundTravelTm = aptList[gencstrlPtr->leg1_arr_aptid].aptMapping[i].duration;
					break;
			   }
			   i++;
		   }
		   //tmp_arr = min(tmp_dt2, dt_addToDateTime(Minutes, optParam.groundTravel, tmp_dt1));
           tmp_arr = dt_addToDateTime(Minutes, groundTravelTm, tmp_dt1);
		   gencstrlPtr->leg3_dpt_time = strdup(dt_DateTimeToDateTimeString(tmp_dpt,tbuf1, "%Y/%m/%d %H:%M"));
		   gencstrlPtr->leg3_arr_time = strdup(dt_DateTimeToDateTimeString(tmp_arr,tbuf2, "%Y/%m/%d %H:%M"));
		   //gencstrlPtr->leg3_dpt_time = strdup(gencstrlPtr->leg1_arr_time);
		   //gencstrlPtr->leg3_arr_time = strdup(request_latest_arr);
		}
		if(rows == 0)
			break;
	  }
	  if(updatetype == 0 && rowCount)//no travel info exists before for this crewID
	  {  
		  insertOneTravelRecord(gencstrlPtr);
	  }
	  else if(rowCount)
	  {
		  updateOneTravelRecord(gencstrlPtr, request_flight_purpose);
	  }
	   
	  mysql_free_result(res);
	  
	  if(!rowCount){
		if ((tmp_dpt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", request_earliest_dep, NULL, &errNbr)) == BadDateTime) {
		  logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, request_earliest_dep);
		  exit(1);
		}

		if ((DateTime_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", request_latest_arr, NULL, &errNbr)) == BadDateTime) {
		  logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, request_latest_arr);
		  exit(1);
		} 
	   comm_mapp_apt_num = 0;
	   i= 0;
	   sprintf(tmp_str1,"(");
	   while(i< aptList[request_dep_aptID].numMaps && aptList[request_dep_aptID].aptMapping[i].duration <=50){
		   mapp_aptID = aptList[request_dep_aptID].aptMapping[i].airportID;
		   mapp_apt_commFlag = aptList[mapp_aptID].commFlag;
		   if(aptList[request_dep_aptID].numMaps == 1){
			 sprintf(tmp_str2,"%d",aptList[request_dep_aptID].aptMapping[i].airportID);
			 strcat(tmp_str1, tmp_str2);
			 comm_mapp_apt_num++;
		   }
		   else if(mapp_apt_commFlag){
			 if(comm_mapp_apt_num ==0)
			   sprintf(tmp_str2,"%d",aptList[request_dep_aptID].aptMapping[i].airportID);
			 else
			   sprintf(tmp_str2,",%d",aptList[request_dep_aptID].aptMapping[i].airportID);
			 strcat(tmp_str1, tmp_str2);
			 comm_mapp_apt_num++;
			 if(comm_mapp_apt_num ==5)
				break;
		   }
		   i++;
	   }
	   if(!comm_mapp_apt_num){
		  if(aptList[request_dep_aptID].numMaps)
			sprintf(tmp_str2,"%d",aptList[request_dep_aptID].aptMapping[0].airportID);
		  else 
			sprintf(tmp_str2,"%d", request_dep_aptID);
		  strcat(tmp_str1, tmp_str2);
	   }
	   strcat(tmp_str1,")");

		sprintf(oag1sql, "\
	select distinct\n\
		dep_aptid,\n\
		arr_aptid,\n\
		FD_flight_number,\n\
		FD_airline_desig,\n\
		UTC_departure_time,\n\
		UTC_arrival_time\n\
	from\n\
		ssim use index(orig_departure_time)\n\
	where\n\
		dep_aptid in %s and\n\
		UTC_departure_time >='%s' and\n\
		UTC_arrival_time <='%s'",
		tmp_str1,
		new_rqst_depTm, 
		//dt_DateTimeToDateTimeString(dt_addToDateTime(Minutes, 6*60, DateTime_dptTm), tbuf1, "%Y/%m/%d %H:%M")
		new_rqst_arrTm
		);

		if(request_flight_purpose == 2)
			strcat(oag1sql,"order by UTC_arrival_time desc");
		else
            strcat(oag1sql,"order by UTC_departure_time asc");
            

	   comm_mapp_apt_num = 0;
	   i= 0;
	   sprintf(tmp_str1,"(");
	   while(i< aptList[request_arr_aptID].numMaps && aptList[request_arr_aptID].aptMapping[i].duration <=50){
		   mapp_aptID = aptList[request_arr_aptID].aptMapping[i].airportID;
		   mapp_apt_commFlag = aptList[mapp_aptID].commFlag;
		   if(aptList[request_arr_aptID].numMaps == 1){
			 sprintf(tmp_str2,"%d",aptList[request_arr_aptID].aptMapping[i].airportID);
			 strcat(tmp_str1, tmp_str2);
			 comm_mapp_apt_num++;
		   }
		   else if(mapp_apt_commFlag){
			 if(comm_mapp_apt_num ==0)
			   sprintf(tmp_str2,"%d",aptList[request_arr_aptID].aptMapping[i].airportID);
			 else
			   sprintf(tmp_str2,",%d",aptList[request_arr_aptID].aptMapping[i].airportID);
			 strcat(tmp_str1, tmp_str2);
			 comm_mapp_apt_num++;
			 if(comm_mapp_apt_num ==5)
				break;
		   }
		   i++;
	   }
	   if(!comm_mapp_apt_num){
		  if(aptList[request_arr_aptID].numMaps)
			sprintf(tmp_str2,"%d",aptList[request_arr_aptID].aptMapping[0].airportID);
		  else 
			sprintf(tmp_str2,"%d", request_arr_aptID);
		  strcat(tmp_str1, tmp_str2);
	   }
	   strcat(tmp_str1,")");
		sprintf(oag2sql, "\
	select\n\
		dep_aptid,\n\
		arr_aptid,\n\
		FD_flight_number,\n\
		FD_airline_desig,\n\
		UTC_departure_time,\n\
		UTC_arrival_time\n\
	from\n\
		ssim use index(dest_arrival_time)\n\
	where\n\
		arr_aptid in %s and\n\
		UTC_departure_time >='%s' and\n\
		UTC_arrival_time <='%s'",
			tmp_str1,
			//dt_DateTimeToDateTimeString(dt_addToDateTime(Minutes, -6*60, DateTime_arrTm),tbuf2, "%Y/%m/%d %H:%M"),
			new_rqst_depTm,
			new_rqst_arrTm	    
		);

		if(request_flight_purpose == 2)
			strcat(oag2sql,"order by UTC_arrival_time desc");
		else
            strcat(oag2sql,"order by UTC_arrival_time asc");

		cjoin_return = cjoin1(myconn_oag, &resultRoot, oag1sql, oag2sql, MIN_LAYOVER, MAX_LAYOVER*2);

		if (cjoin_return) {
			for (tmp = Minimum(resultRoot), onestopNbr = 0; tmp; tmp = Successor(tmp)) {

				resultPtr = (CjoinResult *) getTreeObject(tmp);
				if (!resultPtr->leg1Flight_no[0])
					continue;
				
				if((gencstrlPtr = (GeneratedCsTravel *) calloc((size_t) 1, sizeof(GeneratedCsTravel))) == NULL) {   
					logMsg(logFile,"%s Line %d, Out of Memory in generateOneTravelRecord().\n", __FILE__, __LINE__);
					writeWarningData(myconn); exit(1);
				}
				gencstrlPtr->travellerid = crewID;
				 (*maxrequestid)++;
				gencstrlPtr->requestid = (*maxrequestid);
				gencstrlPtr->leg1_dpt_aptid = resultPtr->leg1OrgAptID;
				gencstrlPtr->leg1_dpt_time =  strdup(dt_DateTimeToDateTimeString(resultPtr->leg1DptTime,tbuf1, "%Y/%m/%d %H:%M"));
				gencstrlPtr->leg1_arr_aptid = resultPtr->leg1DestAptID;
				gencstrlPtr->leg1_arr_time = strdup(dt_DateTimeToDateTimeString(resultPtr->leg1ArrTime,tbuf1, "%Y/%m/%d %H:%M"));
				gencstrlPtr->leg2_dpt_aptid = resultPtr->leg2OrgAptID;
				gencstrlPtr->leg2_dpt_time = strdup(dt_DateTimeToDateTimeString(resultPtr->leg2DptTime,tbuf1, "%Y/%m/%d %H:%M"));
				gencstrlPtr->leg2_arr_aptid = resultPtr->leg2DestAptID;
				gencstrlPtr->leg2_arr_time = strdup(dt_DateTimeToDateTimeString(resultPtr->leg2ArrTime,tbuf1, "%Y/%m/%d %H:%M"));
				gencstrlPtr->directflight = 0;
				gencstrlPtr->groundtravl = 0;
				gencstrlPtr->tix_rqstid_cancelled = tix_rqstid_cancelled;
				if(gencstrlPtr->leg1_dpt_aptid != request_dep_aptID){  
				   gencstrlPtr->groundtravel1 = 1;
				   gencstrlPtr->leg0_dpt_aptid = request_dep_aptID;
				   gencstrlPtr->leg0_arr_aptid = gencstrlPtr->leg1_dpt_aptid;
				   if ((tmp_arr = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg1_dpt_time, NULL, &errNbr)) == BadDateTime) {
					   logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg1_dpt_time);
					   exit(1);
					}
				   tmp_dt1 = dt_addToDateTime(Minutes, -optParam.preBoardTime, tmp_arr);
				   tmp_arr = tmp_dt1;
				   if ((tmp_dt2 = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", request_earliest_dep, NULL, &errNbr)) == BadDateTime) {
					   logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, request_earliest_dep);
					   exit(1);
					}
                    i= 0;
				   while(i< aptList[request_dep_aptID].numMaps){
					   if(aptList[request_dep_aptID].aptMapping[i].airportID == gencstrlPtr->leg1_dpt_aptid){
							groundTravelTm = aptList[request_dep_aptID].aptMapping[i].duration;
							break;
					   }
					   i++;
				   }
				   tmp_dpt = max(tmp_dt2, dt_addToDateTime(Minutes, -groundTravelTm, tmp_dt1));
				   gencstrlPtr->leg0_dpt_time = strdup(dt_DateTimeToDateTimeString(tmp_dpt,tbuf1, "%Y/%m/%d %H:%M"));
				   gencstrlPtr->leg0_arr_time = strdup(dt_DateTimeToDateTimeString(tmp_arr,tbuf2, "%Y/%m/%d %H:%M"));
				   //gencstrlPtr->leg0_dpt_time =  strdup(request_earliest_dep);
				   //gencstrlPtr->leg0_arr_time =  strdup(gencstrlPtr->leg1_dpt_time);  
				}
				if(gencstrlPtr->leg2_arr_aptid != request_arr_aptID){  
				   gencstrlPtr->groundtravel2 = 1;
				   gencstrlPtr->leg3_dpt_aptid = gencstrlPtr->leg2_arr_aptid;
				   gencstrlPtr->leg3_arr_aptid = request_arr_aptID;
				   if ((tmp_dpt = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg2_arr_time, NULL, &errNbr)) == BadDateTime) {
					   logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg2_arr_time);
					   exit(1);
					}
				   tmp_dt1 = dt_addToDateTime(Minutes, optParam.postArrivalTime, tmp_dpt);
				   tmp_dpt = tmp_dt1;
				   if ((tmp_dt2 = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", request_latest_arr, NULL, &errNbr)) == BadDateTime) {
					   logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, request_latest_arr);
					   exit(1);
					}
				   //tmp_arr = min(tmp_dt2, dt_addToDateTime(Minutes, optParam.groundTravel, tmp_dt1));
				   i= 0;
				   while(i< aptList[gencstrlPtr->leg2_arr_aptid].numMaps){
					   if(aptList[gencstrlPtr->leg2_arr_aptid].aptMapping[i].airportID == request_arr_aptID){
							groundTravelTm = aptList[gencstrlPtr->leg2_arr_aptid].aptMapping[i].duration;
							break;
					   }
					   i++;
				   }
				   tmp_arr = dt_addToDateTime(Minutes, groundTravelTm, tmp_dt1);
				   gencstrlPtr->leg3_dpt_time = strdup(dt_DateTimeToDateTimeString(tmp_dpt,tbuf1, "%Y/%m/%d %H:%M"));
				   gencstrlPtr->leg3_arr_time = strdup(dt_DateTimeToDateTimeString(tmp_arr,tbuf2, "%Y/%m/%d %H:%M"));
				   //gencstrlPtr->leg3_dpt_time = strdup(gencstrlPtr->leg1_arr_time);
				   //gencstrlPtr->leg3_arr_time = strdup(request_latest_arr);
				}
				onestopNbr++;
				if(onestopNbr == 1)
					break;
			}
			if(updatetype == 0){ 
					insertOneTravelRecord(gencstrlPtr);
			}
			else{
					updateOneTravelRecord(gencstrlPtr, request_flight_purpose);
			}
		}
		//test
		//else
		//	emptyRecNum++;
		//test
		while (resultRoot) {
			tmp = Minimum(resultRoot);
			resultPtr = (CjoinResult *) getTreeObject(tmp);
			resultRoot = RBTreeDelete(resultRoot, tmp);
			free(resultPtr);
		}
	 }
  }
  else{ 
	    if((gencstrlPtr = (GeneratedCsTravel *) calloc((size_t) 1, sizeof(GeneratedCsTravel))) == NULL) {   
		  logMsg(logFile,"%s Line %d, Out of Memory in generateOneTravelRecord().\n", __FILE__, __LINE__);
		  writeWarningData(myconn); exit(1);
		}
	    gencstrlPtr->groundtravl = 1;
        gencstrlPtr->tix_rqstid_cancelled = tix_rqstid_cancelled;
        //gencstrlPtr->groundtravel1 = 1;
        gencstrlPtr->travellerid = crewID;
	    gencstrlPtr->leg0_dpt_aptid = request_dep_aptID;
	    gencstrlPtr->leg0_arr_aptid = request_arr_aptID;
		(*maxrequestid)++;
	    gencstrlPtr->requestid = (*maxrequestid);
        if ((tmp_dt1 = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", request_earliest_dep, NULL, &errNbr)) == BadDateTime) {
           logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, request_earliest_dep);
	       exit(1);
        }
	    if ((tmp_dt2 = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", request_latest_arr, NULL, &errNbr)) == BadDateTime) {
           logMsg(logFile, "%s Line %d, Bad date in generateOneTravelRecord(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, request_latest_arr);
	       exit(1);
        }
	    //tmp_arr = min(tmp_dt2, dt_addToDateTime(Minutes, optParam.groundTravel, tmp_dpt));
		i= 0;
	    while(i< aptList[request_dep_aptID].numMaps){
		   if(aptList[request_dep_aptID].aptMapping[i].airportID == request_arr_aptID){
				groundTravelTm = aptList[request_dep_aptID].aptMapping[i].duration;
				break;
		   }
		   i++;
	    }
		if(request_flight_purpose == 2){
		   tmp_arr = tmp_dt2;
           tmp_dpt = max(tmp_dt1, dt_addToDateTime(Minutes, -groundTravelTm, tmp_dt2));
		}   
		else{
		   tmp_dpt = tmp_dt1;
		   tmp_arr = dt_addToDateTime(Minutes, groundTravelTm, tmp_dt1);
		}
	    gencstrlPtr->leg0_dpt_time = strdup(dt_DateTimeToDateTimeString(tmp_dpt,tbuf1, "%Y/%m/%d %H:%M"));
	    gencstrlPtr->leg0_arr_time = strdup(dt_DateTimeToDateTimeString(tmp_arr,tbuf2, "%Y/%m/%d %H:%M")); 
		gencstrlPtr->directflight = 1;
        gencstrlPtr->flightpurpose = request_flight_purpose;
		if(updatetype == 0){ 
				insertOneTravelRecord(gencstrlPtr);
		}
		else{
				updateOneTravelRecord(gencstrlPtr, request_flight_purpose);
		}
  }
}


/*******************************************************************************
*	Function:  InsertOneTravelRecord                  1/21/2009
*	Purpose: Insert one commercial itinerary, direct/one-stop  
*             
********************************************************************************/
 void insertOneTravelRecord(GeneratedCsTravel *gencstrlPtr)
 {  //char insertNonstopSQL[1024];
	 CsTravelData *cstrlPtr;
	 int errNbr;
	 
	 if(!gencstrlPtr->groundtravl){
		 if(gencstrlPtr->groundtravel1){   
			if((cstrlPtr = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) {   
			 logMsg(logFile,"%s Line %d, Out of Memory in insertOneTravelRecord().\n", __FILE__, __LINE__);
		     writeWarningData(myconn); exit(1);
		    }
		    cstrlPtr->crewID = gencstrlPtr->travellerid;
	        cstrlPtr->rqtID = gencstrlPtr->requestid;
		    cstrlPtr->dpt_aptID = gencstrlPtr->leg0_dpt_aptid;
		    cstrlPtr->arr_aptID = gencstrlPtr->leg0_arr_aptid;
		    if ((cstrlPtr->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg0_dpt_time, NULL, &errNbr)) == BadDateTime) {
	          logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg0_dpt_time);
		      exit(1);
		    }
			if ((cstrlPtr->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg0_arr_time, NULL, &errNbr)) == BadDateTime) {
			  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg0_arr_time);
			  exit(1);
			}
			if(!(insert_flightRoot = RBTreeInsert(insert_flightRoot, cstrlPtr, travelFlightCmp))) {
			  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in insertOneTravelRecord().\n",__FILE__,__LINE__);
			  exit(1);
			}
		 }
        if((cstrlPtr = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) {   
			logMsg(logFile,"%s Line %d, Out of Memory in insertOneTravelRecord().\n", __FILE__, __LINE__);
		    writeWarningData(myconn); exit(1);
	    }
		cstrlPtr->crewID = gencstrlPtr->travellerid;
	    cstrlPtr->rqtID = gencstrlPtr->requestid;
		cstrlPtr->dpt_aptID = gencstrlPtr->leg1_dpt_aptid;
		cstrlPtr->arr_aptID = gencstrlPtr->leg1_arr_aptid;
		if ((cstrlPtr->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg1_dpt_time, NULL, &errNbr)) == BadDateTime) {
	      logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg1_dpt_time);
		  exit(1);
		}
		if ((cstrlPtr->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg1_arr_time, NULL, &errNbr)) == BadDateTime) {
	      logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg1_arr_time);
		  exit(1);
		}
		if(!(insert_flightRoot = RBTreeInsert(insert_flightRoot, cstrlPtr, travelFlightCmp))) {
		  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in insertOneTravelRecord().\n",__FILE__,__LINE__);
		  exit(1);
		}
		
		if(!gencstrlPtr->directflight){
			if((cstrlPtr = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) {   
				logMsg(logFile,"%s Line %d, Out of Memory in insertOneTravelRecord().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			}
			cstrlPtr->crewID = gencstrlPtr->travellerid;
			cstrlPtr->rqtID = gencstrlPtr->requestid;
			cstrlPtr->dpt_aptID = gencstrlPtr->leg2_dpt_aptid;
			cstrlPtr->arr_aptID = gencstrlPtr->leg2_arr_aptid;
			if ((cstrlPtr->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg2_dpt_time, NULL, &errNbr)) == BadDateTime) {
			  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg2_dpt_time);
			  exit(1);
			}
			if ((cstrlPtr->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg2_arr_time, NULL, &errNbr)) == BadDateTime) {
			  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg2_arr_time);
			  exit(1);
			}
			if(!(insert_flightRoot = RBTreeInsert(insert_flightRoot, cstrlPtr, travelFlightCmp))) {
			  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in insertOneTravelRecord().\n",__FILE__,__LINE__);
			  exit(1);
			}
		}

		if(gencstrlPtr->groundtravel2){   
			if((cstrlPtr = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) {   
			 logMsg(logFile,"%s Line %d, Out of Memory in insertOneTravelRecord().\n", __FILE__, __LINE__);
		     writeWarningData(myconn); exit(1);
		    }
		    cstrlPtr->crewID = gencstrlPtr->travellerid;
	        cstrlPtr->rqtID = gencstrlPtr->requestid;
		    cstrlPtr->dpt_aptID = gencstrlPtr->leg3_dpt_aptid;
		    cstrlPtr->arr_aptID = gencstrlPtr->leg3_arr_aptid;
		    if ((cstrlPtr->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg3_dpt_time, NULL, &errNbr)) == BadDateTime) {
	          logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg3_dpt_time);
		      exit(1);
		    }
			if ((cstrlPtr->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg3_arr_time, NULL, &errNbr)) == BadDateTime) {
			  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg3_arr_time);
			  exit(1);
			}
			if(!(insert_flightRoot = RBTreeInsert(insert_flightRoot, cstrlPtr, travelFlightCmp))) {
			  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in insertOneTravelRecord().\n",__FILE__,__LINE__);
			  exit(1);
			}
		}
	 }
        //test
		//insertNum++;
        //test
   /* sprintf(insertNonstopSQL, "insert into cs_travel_flights values (%d, '%s', %d, %d, '%s', '%s', %d)",
		    traveller_id,
			"CLOSED",
			dpt_aptid,
			arr_aptid,
			dpt_time,
			arr_time,
			requestid);
	if(!(myDoWrite(myconn,insertNonstopSQL))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
     }*/
	 else{
           if((cstrlPtr = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) {   
			 logMsg(logFile,"%s Line %d, Out of Memory in insertOneTravelRecord().\n", __FILE__, __LINE__);
		     writeWarningData(myconn); exit(1);
		    }
		    cstrlPtr->crewID = gencstrlPtr->travellerid;
	        cstrlPtr->rqtID = gencstrlPtr->requestid;
		    cstrlPtr->dpt_aptID = gencstrlPtr->leg0_dpt_aptid;
		    cstrlPtr->arr_aptID = gencstrlPtr->leg0_arr_aptid;
		    if ((cstrlPtr->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg0_dpt_time, NULL, &errNbr)) == BadDateTime) {
	          logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg0_dpt_time);
		      exit(1);
		    }
			if ((cstrlPtr->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg0_arr_time, NULL, &errNbr)) == BadDateTime) {
			  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg0_arr_time);
			  exit(1);
			}
			if(!(insert_flightRoot = RBTreeInsert(insert_flightRoot, cstrlPtr, travelFlightCmp))) {
			  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in insertOneTravelRecord().\n",__FILE__,__LINE__);
			  exit(1);
			}
	 }
 }


/*******************************************************************************
*	Function:  updateOneTravelRecord                  1/21/2009
*	Purpose: Update one record of commercial itinerary (direct/one stop) accroding in cs_travel_flights
*        
********************************************************************************/
 void updateOneTravelRecord(GeneratedCsTravel *gencstrlPtr, int flight_purpose)
 {
     char UpdateNonstopSQL[1024];
	 CsTravelData *cstrlBuf, *cstrlPtr, *cstrlPtr1, *cstrlPtr2, *tmp_cstrlPtr, *cstrlPtr0, *cstrlPtr3;
	 BINTREENODE *tmp;
	 DateTime dt_pwStart;
	 DateTime travel_dpt_time, travel_arr_time;
	 int insert_indicator = 9999;// indicate if insert a newly queried travel record.
	 int endwithsameID;//if the last node with same CrewID
	 int errNbr;
	 LookupRet lkRet;
	 DateTime tmp_dt1, tmp_dt2;


	 dt_pwStart = dt_time_tToDateTime(optParam.windowStart);
	 tmp_dt1 = dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_run_time_GMT);
	 if(!gencstrlPtr->groundtravl){
		 if(gencstrlPtr->groundtravel1){
             if ((travel_dpt_time = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg0_dpt_time, NULL, &errNbr)) == BadDateTime) {
	            logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg0_dpt_time);
		        exit(1);
			 }
		 }
		 else{
             if ((travel_dpt_time = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg1_dpt_time, NULL, &errNbr)) == BadDateTime) {
	            logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg1_dpt_time);
		        exit(1);
			 }
		 }
         if(gencstrlPtr->groundtravel2){
			 if ((travel_arr_time = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg3_arr_time, NULL, &errNbr)) == BadDateTime) {
	            logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg3_arr_time);
		        exit(1);
			 }   
		 }
		 else{
             if ((travel_arr_time = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->directflight? gencstrlPtr->leg1_arr_time: gencstrlPtr->leg2_arr_time, NULL, &errNbr)) == BadDateTime) {
	            logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->directflight? gencstrlPtr->leg1_arr_time: gencstrlPtr->leg2_arr_time);
		        exit(1);
			 } 
		 }
	 } 
	 else{
         if ((travel_dpt_time = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg0_dpt_time, NULL, &errNbr)) == BadDateTime) {
	            logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg0_dpt_time);
		        exit(1);
			 }
		 if ((travel_arr_time = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg0_arr_time, NULL, &errNbr)) == BadDateTime) {
	            logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg0_arr_time);
		        exit(1);
			 }  
	 }
	 
	 if((cstrlBuf = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) {   
		 logMsg(logFile,"%s Line %d, Out of Memory in updateOneTravelRecord().\n", __FILE__, __LINE__);
		 writeWarningData(myconn); exit(1);
	 }
	 memset(cstrlBuf, '\0', sizeof(CsTravelData));
	 cstrlBuf->crewID = gencstrlPtr->travellerid;
	 lkRet = Lookup(concurr_flightRoot, cstrlBuf, travelFlightCmp, &tmp);
	 switch(lkRet) {
	 case EmptyTree:
	 case GreaterThanLastItem:
	 case ExactMatchFound: // impossible, since we just entered crewid
		//It is possible, no travel data.
		logMsg(logFile,"%s Line %d: crewid %d not found in existing_travel_flights data. lkRet=%d\n", __FILE__,__LINE__, gencstrlPtr->travellerid, lkRet);
		break;
	 case NotFoundReturnedNextItem:
		 for(; tmp; tmp = Successor(tmp)){
			 cstrlPtr = (CsTravelData *) getTreeObject(tmp);
			 if(cstrlPtr->crewID != gencstrlPtr->travellerid)
				break;
			 if(Successor(tmp)){
			   tmp_cstrlPtr = (CsTravelData *) getTreeObject(Successor(tmp));
               if(tmp_cstrlPtr->crewID == cstrlPtr->crewID)
                    continue;
			   else
				    break;
			 }
		    else
				    break;

		 }
		 break;
	 }
			 /*{
			cstrlPtr = (CsTravelData *) getTreeObject(tmp);
			if(cstrlPtr->crewID != gencstrlPtr->travellerid)
				break;
			//
			if(cstrlPtr->changed)
				continue;
			//
			if(Successor(tmp)){
			   tmp_cstrlPtr = (CsTravelData *) getTreeObject(Successor(tmp));
               if(tmp_cstrlPtr->crewID == gencstrlPtr->travellerid)
                    endwithsameID = 0;
			   else
				    endwithsameID = 1;
			}
			else
				    endwithsameID = 1;
			tmp_dt2 = dt_addToDateTime(Minutes, -optParam.preBoardTime, cstrlPtr->travel_dptTm);
				
			if ( tmp_dt2 <= tmp_dt1){
				if(endwithsameID == 1){
				  cstrlPtr1->inserted = 1;
                  cstrlPtr1->changed = 1;
                  if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr1, travelFlightCmp))) {
				     logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
				     writeWarningData(myconn); exit(1);
				  }
				  if(!gencstrlPtr->directflight){
				     cstrlPtr2->inserted = 1;
					 cstrlPtr2->changed = 1;
                     if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr2, travelFlightCmp))) {
						 logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
						 writeWarningData(myconn); exit(1);
					 }
				  }
                  if((concurr_crewListEffected = (int *)realloc((concurr_crewListEffected),(concurr_crewnumbereffected+1)*sizeof(int))) == NULL) 
		          { logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		            writeWarningData(myconn); exit(1);
	              }
				  concurr_crewListEffected[concurr_crewnumbereffected] = cstrlPtr1->crewID;
                  concurr_crewnumbereffected++;
//                  insertNum++;
				  break;
				}
				else
				  continue;	  
			}
			else if (travel_dpt_time <= cstrlPtr->travel_arrTm && travel_arr_time >= cstrlPtr->travel_dptTm){
				//if((flight_purpose == 1 && checkAptNearby(cstrlPtr->dpt_aptID, gencstrlPtr->leg1_dpt_aptid))||(flight_purpose == 2 && checkAptNearby(cstrlPtr->arr_aptID, gencstrlPtr->directflight? gencstrlPtr->leg1_arr_aptid : gencstrlPtr->leg2_arr_aptid))){
			        insert_indicator = 0;
				//}
			}
			else{
				if(flight_purpose == 1 && checkAptNearby(cstrlPtr->dpt_aptID, gencstrlPtr->leg1_dpt_aptid))
					insert_indicator = 0;
					
				else if(flight_purpose == 1 && checkAptNearby(cstrlPtr->arr_aptID, gencstrlPtr->leg1_dpt_aptid))
					insert_indicator = 1;

				if(flight_purpose == 2){
				   if(endwithsameID)
					 insert_indicator = 0;
				}
			}
			if(insert_indicator == 0)
			{  	 
				   cstrlPtr->dpt_aptID = cstrlPtr1->dpt_aptID;
				   cstrlPtr->arr_aptID = cstrlPtr1->arr_aptID;
				   cstrlPtr->travel_dptTm = cstrlPtr1->travel_dptTm;
				   cstrlPtr->travel_arrTm = cstrlPtr1->travel_arrTm;
				   cstrlPtr->changed = 1;
				   cstrlPtr->updated = 1;
                   cstrlPtr->pre_rqtID = cstrlPtr->rqtID;
				   cstrlPtr->rqtID = cstrlPtr1->rqtID;                   
				   if(!gencstrlPtr->directflight){
				      cstrlPtr2->updated = 1;
                      cstrlPtr2->changed = 1;
                      cstrlPtr2->pre_rqtID = cstrlPtr->pre_rqtID; 
                      if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr2, travelFlightCmp))) {
				        logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
				        writeWarningData(myconn); exit(1);
				      }
				   }
                   if((concurr_crewListEffected = (int *)realloc((concurr_crewListEffected),(concurr_crewnumbereffected+1)*sizeof(int))) == NULL){ 
          			   logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	               }
				   concurr_crewListEffected[concurr_crewnumbereffected] = cstrlPtr1->crewID;
                   concurr_crewnumbereffected++; 
//				   updatedNum++;
					break;
			}
			else if(insert_indicator == 1){
				cstrlPtr1->inserted = 1;
				cstrlPtr1->changed = 1;
				if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr1, travelFlightCmp))) {
				  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
				  writeWarningData(myconn); exit(1);
				}
				if(!gencstrlPtr->directflight){
					  cstrlPtr2->inserted = 1;
					  cstrlPtr2->changed = 1;
                      if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr2, travelFlightCmp))) {
				        logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
				        writeWarningData(myconn); exit(1);
				      }
				}
				if((concurr_crewListEffected = (int *)realloc((concurr_crewListEffected),(concurr_crewnumbereffected+1)*sizeof(int))) == NULL){ 
          			   logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	            }
			    concurr_crewListEffected[concurr_crewnumbereffected] = cstrlPtr1->crewID;
                concurr_crewnumbereffected++; 
//				insertNum++;
				break;
			}
		 }*/
	
     tmp_dt2 = dt_addToDateTime(Minutes, -optParam.preBoardTime, cstrlPtr->travel_dptTm);
	 if ( tmp_dt2 <= tmp_dt1)
		 insert_indicator = 1;
	 else{
		 if(gencstrlPtr->tix_rqstid_cancelled)
            insert_indicator = 2;
		 else{
			 if(!gencstrlPtr->groundtravl){
				 if(gencstrlPtr->groundtravel1){
					 if(cstrlPtr->arr_aptID == gencstrlPtr->leg0_dpt_aptid)
                        gencstrlPtr->requestid = cstrlPtr->rqtID;
				 }
				 else{ 
					 if(cstrlPtr->arr_aptID == gencstrlPtr->leg1_dpt_aptid)
					    gencstrlPtr->requestid = cstrlPtr->rqtID;
				 }
			 }
			 else{
				 if(cstrlPtr->arr_aptID == gencstrlPtr->leg0_dpt_aptid)
                        gencstrlPtr->requestid = cstrlPtr->rqtID; 
			 }
		    insert_indicator = 1;
		 }
	 }
	 if(!gencstrlPtr->groundtravl){
		 if(gencstrlPtr->groundtravel1){
			 if((cstrlPtr0 = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) {   
				 logMsg(logFile,"%s Line %d, Out of Memory in updateOneTravelRecord().\n", __FILE__, __LINE__);
				 writeWarningData(myconn); exit(1);
			 }
			 cstrlPtr0->crewID = gencstrlPtr->travellerid;
			 cstrlPtr0->rqtID = gencstrlPtr->requestid;
			 cstrlPtr0->dpt_aptID = gencstrlPtr->leg0_dpt_aptid;
			 cstrlPtr0->arr_aptID = gencstrlPtr->leg0_arr_aptid;
			 if ((cstrlPtr0->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg0_dpt_time, NULL, &errNbr)) == BadDateTime) {
				logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg0_dpt_time);
				exit(1);
			 }
			 if ((cstrlPtr0->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg0_arr_time, NULL, &errNbr)) == BadDateTime) {
				  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg0_arr_time);
				  exit(1);
			 }	     
		 }

		 if((cstrlPtr1 = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) {   
			 logMsg(logFile,"%s Line %d, Out of Memory in updateOneTravelRecord().\n", __FILE__, __LINE__);
			 writeWarningData(myconn); exit(1);
		 }
		 cstrlPtr1->crewID = gencstrlPtr->travellerid;
		 cstrlPtr1->rqtID = gencstrlPtr->requestid;
		 cstrlPtr1->dpt_aptID = gencstrlPtr->leg1_dpt_aptid;
		 cstrlPtr1->arr_aptID = gencstrlPtr->leg1_arr_aptid;
		 if ((cstrlPtr1->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg1_dpt_time, NULL, &errNbr)) == BadDateTime) {
			  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg1_dpt_time);
			  exit(1);
		 }
		 if ((cstrlPtr1->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg1_arr_time, NULL, &errNbr)) == BadDateTime) {
			  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg1_arr_time);
			  exit(1);
		 }

		 if(!gencstrlPtr->directflight){
			 if((cstrlPtr2 = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL){   
				 logMsg(logFile,"%s Line %d, Out of Memory in updateOneTravelRecord().\n", __FILE__, __LINE__);
				 writeWarningData(myconn); exit(1);
			 }
			  cstrlPtr2->crewID = gencstrlPtr->travellerid;
			  cstrlPtr2->rqtID = gencstrlPtr->requestid;
			  cstrlPtr2->dpt_aptID = gencstrlPtr->leg2_dpt_aptid;
			  cstrlPtr2->arr_aptID = gencstrlPtr->leg2_arr_aptid;
			 if ((cstrlPtr2->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg2_dpt_time, NULL, &errNbr)) == BadDateTime) {
				  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg2_dpt_time);
				  exit(1);
			 }
			 if ((cstrlPtr2->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg2_arr_time, NULL, &errNbr)) == BadDateTime) {
				  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg2_arr_time);
				  exit(1);
			 }
		 }

		 if(gencstrlPtr->groundtravel2){
			 if((cstrlPtr3 = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) {   
				 logMsg(logFile,"%s Line %d, Out of Memory in updateOneTravelRecord().\n", __FILE__, __LINE__);
				 writeWarningData(myconn); exit(1);
			 }
			 cstrlPtr3->crewID = gencstrlPtr->travellerid;
			 cstrlPtr3->rqtID = gencstrlPtr->requestid;
			 cstrlPtr3->dpt_aptID = gencstrlPtr->leg3_dpt_aptid;
			 cstrlPtr3->arr_aptID = gencstrlPtr->leg3_arr_aptid;
			 if ((cstrlPtr3->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg3_dpt_time, NULL, &errNbr)) == BadDateTime) {
				 logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg3_dpt_time);
				 exit(1);
			 }
			 if ((cstrlPtr3->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg3_arr_time, NULL, &errNbr)) == BadDateTime) {
				  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg3_arr_time);
				  exit(1);
			 }	     
		 }
	 }
 	 else{
			if((cstrlPtr0 = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) {   
				 logMsg(logFile,"%s Line %d, Out of Memory in updateOneTravelRecord().\n", __FILE__, __LINE__);
				 writeWarningData(myconn); exit(1);
			 }
			 cstrlPtr0->crewID = gencstrlPtr->travellerid;
			 cstrlPtr0->rqtID = gencstrlPtr->requestid;
			 cstrlPtr0->dpt_aptID = gencstrlPtr->leg0_dpt_aptid;
			 cstrlPtr0->arr_aptID = gencstrlPtr->leg0_arr_aptid;
			 if ((cstrlPtr0->travel_dptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg0_dpt_time, NULL, &errNbr)) == BadDateTime) {
				logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg0_dpt_time);
				exit(1);
			 }
			 if ((cstrlPtr0->travel_arrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", gencstrlPtr->leg0_arr_time, NULL, &errNbr)) == BadDateTime) {
				  logMsg(logFile, "%s Line %d, Bad date in updateCsTravelFlights(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, gencstrlPtr->leg0_arr_time);
				  exit(1);
			 }	      
	 }
	 if(insert_indicator == 1){
		 if(!gencstrlPtr->groundtravl){
				  if(gencstrlPtr->groundtravel1){
					   cstrlPtr0->inserted = 1;
                       cstrlPtr0->changed = 1;
                       if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr0, travelFlightCmp))) {
						 logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
						 writeWarningData(myconn); exit(1);
					   }
				  }
				  cstrlPtr1->inserted = 1;
                  cstrlPtr1->changed = 1;
                  if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr1, travelFlightCmp))) {
				     logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
				     writeWarningData(myconn); exit(1);
				  }
				  if(!gencstrlPtr->directflight){
				     cstrlPtr2->inserted = 1;
					 cstrlPtr2->changed = 1;
                     if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr2, travelFlightCmp))) {
						 logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
						 writeWarningData(myconn); exit(1);
					 }
				  }
				  if(gencstrlPtr->groundtravel2){
                       cstrlPtr3->inserted = 1;
                       cstrlPtr3->changed = 1;
                       if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr3, travelFlightCmp))) {
						 logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
						 writeWarningData(myconn); exit(1);
					   }
				  }
                  if((concurr_crewListEffected = (int *)realloc((concurr_crewListEffected),(concurr_crewnumbereffected+1)*sizeof(int))) == NULL) 
		          { logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		            writeWarningData(myconn); exit(1);
	              }
				  concurr_crewListEffected[concurr_crewnumbereffected] = cstrlPtr1->crewID;
                  concurr_crewnumbereffected++;
		 }
		 else{
                   cstrlPtr0->inserted = 1;
                   cstrlPtr0->changed = 1;
                   if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr0, travelFlightCmp))) {
					 logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
					 writeWarningData(myconn); exit(1);
				   }
				   if((concurr_crewListEffected = (int *)realloc((concurr_crewListEffected),(concurr_crewnumbereffected+1)*sizeof(int))) == NULL) 
		          { logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		            writeWarningData(myconn); exit(1);
	              }
				  concurr_crewListEffected[concurr_crewnumbereffected] = cstrlPtr0->crewID;
                  concurr_crewnumbereffected++;
		 }
	 }
	 if(insert_indicator == 2){
		 if(!gencstrlPtr->groundtravl){
			         if(gencstrlPtr->groundtravel1){
						   cstrlPtr->dpt_aptID = cstrlPtr0->dpt_aptID;
						   cstrlPtr->arr_aptID = cstrlPtr0->arr_aptID;
						   cstrlPtr->travel_dptTm = cstrlPtr0->travel_dptTm;
						   cstrlPtr->travel_arrTm = cstrlPtr0->travel_arrTm;
						   cstrlPtr->changed = 1;
						   cstrlPtr->updated = 1;
						   cstrlPtr->pre_rqtID = cstrlPtr->rqtID;
						   cstrlPtr->rqtID = gencstrlPtr->requestid;   
						   cstrlPtr1->inserted = 1;
						   cstrlPtr1->changed = 1;
						   cstrlPtr1->pre_rqtID = cstrlPtr->pre_rqtID;
						   cstrlPtr1->rqtID = gencstrlPtr->requestid;  
						   if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr1, travelFlightCmp))) {
							 logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
							 writeWarningData(myconn); exit(1);
						   }
					 }
					 else{
						   cstrlPtr->dpt_aptID = cstrlPtr1->dpt_aptID;
						   cstrlPtr->arr_aptID = cstrlPtr1->arr_aptID;
						   cstrlPtr->travel_dptTm = cstrlPtr1->travel_dptTm;
						   cstrlPtr->travel_arrTm = cstrlPtr1->travel_arrTm;
						   cstrlPtr->changed = 1;
						   cstrlPtr->updated = 1;
						   cstrlPtr->pre_rqtID = cstrlPtr->rqtID;
						   cstrlPtr->rqtID = gencstrlPtr->requestid;   
					 }

				   if(!gencstrlPtr->directflight){
				      cstrlPtr2->inserted = 1;
                      cstrlPtr2->changed = 1;
                      cstrlPtr2->pre_rqtID = cstrlPtr->pre_rqtID; 
					  cstrlPtr2->rqtID = gencstrlPtr->requestid;  
                      if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr2, travelFlightCmp))) {
				        logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
				        writeWarningData(myconn); exit(1);
				      }
				   }

				   if(gencstrlPtr->groundtravel2)
				   {  cstrlPtr3->inserted = 1;
                      cstrlPtr3->changed = 1;
                      cstrlPtr3->pre_rqtID = cstrlPtr->pre_rqtID; 
					  cstrlPtr3->rqtID = gencstrlPtr->requestid;  
                      if(!(concurr_flightRoot = RBTreeInsert(concurr_flightRoot, cstrlPtr3, travelFlightCmp))) {
				        logMsg(logFile,"%s Line %d, RBTreeInsert() failed in updateOneTravelRecord().\n",__FILE__,__LINE__);
				        writeWarningData(myconn); exit(1);
				      }
				   }
			 
                   if((concurr_crewListEffected = (int *)realloc((concurr_crewListEffected),(concurr_crewnumbereffected+1)*sizeof(int))) == NULL){ 
          			   logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	               }
				   concurr_crewListEffected[concurr_crewnumbereffected] = cstrlPtr->crewID;
                   concurr_crewnumbereffected++; 
		 }
		 else{
                   cstrlPtr->dpt_aptID = cstrlPtr0->dpt_aptID;
			       cstrlPtr->arr_aptID = cstrlPtr0->arr_aptID;
				   cstrlPtr->travel_dptTm = cstrlPtr0->travel_dptTm;
				   cstrlPtr->travel_arrTm = cstrlPtr0->travel_arrTm;
				   cstrlPtr->changed = 1;
				   cstrlPtr->updated = 1;
				   cstrlPtr->pre_rqtID = cstrlPtr->rqtID;
				   cstrlPtr->rqtID = gencstrlPtr->requestid;
				   if((concurr_crewListEffected = (int *)realloc((concurr_crewListEffected),(concurr_crewnumbereffected+1)*sizeof(int))) == NULL){ 
          			   logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	               }
				   concurr_crewListEffected[concurr_crewnumbereffected] = cstrlPtr->crewID;
                   concurr_crewnumbereffected++; 
		 }
	 }
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

/*******************************************************************************
*	Function:  cjoin1                  1/21/2009
*	Purpose: find one stop itinerary by doing joint search
*        
********************************************************************************/

typedef enum {
    LG_dep_aptid = 0,
    LG_arr_aptid,
    LG_FD_flight_number,
    LG_FD_airline_desig,
    LG_UTC_departure_time,
    LG_UTC_arrival_time,
    LG_nbr_items
};


int
cjoin1(MY_CONNECTION *myconn, BINTREENODE **result, char *oag1sql, char *oag2sql, int MIN_Layover, int MAX_Layover)
{
        int errNbr;

	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	MYSQL_ROW row;
	my_ulonglong rows;
//	char tbuf1[32], tbuf2[32], tbuf3[32];

//	int i;
	LEG1 *leg1Ptr, leg1Buf;
	BINTREENODE *tmp;
	LookupRet lkRet;
	time_t leg1_arrival_t, leg2_depart_t;
	DateTime leg2_depart_dt;
	int rowMatches;

	char sqlStmt[2048];

	CjoinResult *resultPtr;
	//test
	  int test =0;
   //test
	*result = NULL;
	rowMatches = 0;

	
	


	

	if(!myDoQuery(myconn, oag1sql, &res, &cols)) {
		fprintf(stderr,"db errno: %d: %s\n", myconn->my_errno, myconn->my_error_msg);
		exit(1);
	}
	if(res) {
		for(rows = 0; rows < myconn->rowCount; rows++) {
			row = mysql_fetch_row(res);
			if(! row)
				break;
			// install row in binary tree
			if(!(leg1Ptr = (LEG1 *) calloc(1,sizeof(LEG1)))) {
				fprintf(stderr,"%s Line %d: Out of memory\n", __FILE__,__LINE__);
				exit(1);
			}
			leg1Ptr->dep_aptid = atoi(row[LG_dep_aptid]);
			leg1Ptr->arr_aptid = atoi(row[LG_arr_aptid]);
			strcpy(leg1Ptr->FD_airline_desig, row[LG_FD_airline_desig]);
			strcpy(leg1Ptr->FD_flight_number, row[LG_FD_flight_number]);
			if((leg1Ptr->UTC_departure_time =
			    dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[LG_UTC_departure_time], NULL, &errNbr)) == BadDateTime) {
				fprintf(stderr,"%s Line %d: Bad Date Time: %s\n", __FILE__,__LINE__,row[LG_UTC_departure_time]);
				exit(1);
			}
			if((leg1Ptr->UTC_arrival_time =
			    dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[LG_UTC_arrival_time], NULL, &errNbr)) == BadDateTime) {
				fprintf(stderr,"%s Line %d: Bad Date Time: %s\n", __FILE__,__LINE__,row[LG_UTC_arrival_time]);
				exit(1);
			}
			if(!(leg1Root = RBTreeInsert(leg1Root, leg1Ptr, myleg1Compare))) {
				fprintf(stderr,"%s Line %d: TreeInsert() failed\n", __FILE__,__LINE__);
				fprintf(stderr,"SQL:\n%s\n", sqlStmt);
				myshowLeg1(leg1Ptr);
				exit(1);
			}
			test++;
		}
		mysql_free_result(res);

		if(!myDoQuery(myconn, oag2sql, &res, &cols)) {
			fprintf(stderr,"db errno: %d: %s\n", myconn->my_errno, myconn->my_error_msg);
			exit(1);
		}
		if(res) {
			for(rows = 0; rows < myconn->rowCount; rows++) {
				row = mysql_fetch_row(res);
				if(! row)
					break;
				leg1Buf.arr_aptid = atoi(row[LG_dep_aptid]);
				//strcpy(leg1Buf.FD_airline_desig, row[LG_FD_airline_desig]);
				leg1Buf.FD_airline_desig[0] = '\0';
				lkRet = Lookup(leg1Root, &leg1Buf, myleg1Compare, &tmp);
				switch(lkRet) {
				case ExactMatchFound: // impossible, since we don't enter FD_flight_number
					fprintf(stderr,"%s Line %d: found null flight number.\n", __FILE__,__LINE__);
					continue;
				case NotFoundReturnedNextItem:
					for(;tmp; tmp = Successor(tmp)) {
						leg1Ptr = (LEG1 *) getTreeObject(tmp);
						//if(!(strcmp(leg1Ptr->FD_airline_desig,row[LG_FD_airline_desig]) == 0 && leg1Ptr->arr_aptid == atoi(row[LG_dep_aptid])))
						  if(!(leg1Ptr->arr_aptid == atoi(row[LG_dep_aptid])))
							break;
						leg1_arrival_t = DateTimeToTime_t(leg1Ptr->UTC_arrival_time);
						if((leg2_depart_dt =
			    			    dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[LG_UTC_departure_time], NULL, &errNbr)) == BadDateTime) {
							fprintf(stderr,"%s Line %d: Bad Date Time: %s\n", __FILE__,__LINE__,row[LG_UTC_departure_time]);
							exit(1);
						}
						leg2_depart_t = DateTimeToTime_t(leg2_depart_dt);
						if((leg2_depart_t - leg1_arrival_t >= MIN_Layover) && (leg2_depart_t - leg1_arrival_t <= MAX_Layover)) {
							if(!(resultPtr = (CjoinResult *) calloc(1, sizeof(CjoinResult)))) {
								fprintf(stderr,"%s Line %d: Out of memory.\n",__FILE__,__LINE__);
								exit(1);
							}
							resultPtr->leg1OrgAptID = leg1Ptr->dep_aptid; 
							resultPtr->leg1DestAptID =  leg1Ptr->arr_aptid;
							strcpy(resultPtr->leg1Flight_no, leg1Ptr->FD_flight_number);
							strcpy(resultPtr->leg1Flight_carrier,leg1Ptr->FD_airline_desig);
							resultPtr->leg1DptTime = leg1Ptr->UTC_departure_time;
							resultPtr->leg1ArrTime = leg1Ptr->UTC_arrival_time;
							resultPtr->leg2OrgAptID = atoi(row[LG_dep_aptid]);
							resultPtr->leg2DestAptID = atoi(row[LG_arr_aptid]);
							strcpy(resultPtr->leg2Flight_no,row[LG_FD_flight_number]);
							strcpy(resultPtr->leg2Flight_carrier,row[LG_FD_airline_desig]);
							if((resultPtr->leg2DptTime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d",
								row[LG_UTC_departure_time], NULL, &errNbr)) == BadDateTime) {
								fprintf(stderr,"%s Line %d: Bad Date Time: %s\n", __FILE__,__LINE__,row[LG_UTC_departure_time]);
								exit(1);
							}
							if((resultPtr->leg2ArrTime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d",
								row[LG_UTC_arrival_time], NULL, &errNbr)) == BadDateTime) {
								fprintf(stderr,"%s Line %d: Bad Date Time: %s\n", __FILE__,__LINE__,row[LG_UTC_arrival_time]);
								exit(1);
							}
							if(!TreeSearch(*result,resultPtr,myresultCompare))
								*result = RBTreeInsert(*result, resultPtr, myresultCompare);
							else
								continue;
							rowMatches++;
							//only for simulation
							if(rowMatches >= 5)
								break;
						}
					}
					break;
				}
                if(rowMatches >= 5)
				  break;
			}
			mysql_free_result(res);
		}
	}
	while(leg1Root) {
		tmp = Minimum(leg1Root);
		leg1Ptr = (LEG1*) getTreeObject(tmp);
		leg1Root = RBTreeDelete(leg1Root, tmp);
		free(leg1Ptr);
	}
	return (rowMatches);
}


static int
myleg1Compare(void *a, void *b)
{
	int ret;
	LEG1 *a1 = (LEG1 *) a;
	LEG1 *b1 = (LEG1 *) b;

	if((ret = a1->arr_aptid - b1->arr_aptid))
		return(ret);
	if((ret = strcmp(a1->FD_airline_desig,b1->FD_airline_desig)))
		return(ret);
	if((ret = strcmp(a1->FD_flight_number,b1->FD_flight_number)))
		return(ret);
	if(a1->UTC_departure_time > b1->UTC_departure_time)
		return(1);
	if(a1->UTC_departure_time < b1->UTC_departure_time)
		return(-1);
	if(a1->UTC_arrival_time > b1->UTC_arrival_time)
		return(1);
	if(a1->UTC_arrival_time < b1->UTC_arrival_time)
		return(-1);
	return(0);
}

static int
myresultCompare(void *a, void *b)
{
	int ret;
	CjoinResult *a1 = (CjoinResult *) a;
	CjoinResult *b1 = (CjoinResult *) b;

        if((ret = a1->leg1OrgAptID - b1->leg1OrgAptID))
		return(ret);

        if((ret = a1->leg1DestAptID - b1->leg1DestAptID))
		return(ret);

	if((ret = strcmp(a1->leg1Flight_no, b1->leg1Flight_no)))
		return(ret);

        if((ret = strcmp(a1->leg1Flight_carrier, b1->leg1Flight_carrier)))
		return(ret);

        if(a1->leg1DptTime > b1->leg1DptTime)
		return(1);
        if(a1->leg1DptTime < b1->leg1DptTime)
		return(-1);

        if(a1->leg1ArrTime > b1->leg1ArrTime)
		return(1);
        if(a1->leg1ArrTime < b1->leg1ArrTime)
		return(-1);


        if((ret = a1->leg2OrgAptID - b1->leg2OrgAptID))
		return(ret);

        if((ret = a1->leg2DestAptID - b1->leg2DestAptID))
		return(ret);

	if((ret = strcmp(a1->leg2Flight_no, b1->leg2Flight_no)))
		return(ret);

        if((ret = strcmp(a1->leg2Flight_carrier, b1->leg2Flight_carrier)))
		return(ret);

        if(a1->leg2DptTime > b1->leg2DptTime)
		return(1);
        if(a1->leg2DptTime < b1->leg2DptTime)
		return(-1);

        if(a1->leg2ArrTime > b1->leg2ArrTime)
		return(1);
        if(a1->leg2ArrTime < b1->leg2ArrTime)
		return(-1);
	return(0);
}

static void myshowLeg1(LEG1 *leg1Ptr)
{
	char tbuf1[32], tbuf2[32];

	fprintf(stdout,"%4d %4d %3s %6s %s %s\n",
		leg1Ptr->dep_aptid,
		leg1Ptr->arr_aptid,
		leg1Ptr->FD_airline_desig,
		leg1Ptr->FD_flight_number,
		dt_DateTimeToDateTimeString(leg1Ptr->UTC_departure_time, tbuf1, "%Y/%m/%d %H:%M"),
		dt_DateTimeToDateTimeString(leg1Ptr->UTC_arrival_time, tbuf2, "%Y/%m/%d %H:%M"));
}
void writeProposedTravelFlights(MY_CONNECTION *conn, int scenarioid)
{
	BINTREENODE *tmp;
	CsTravelData *cstrlBuf, *cstrlPtr;
	char opbuf1[32], opbuf2[32];
    char sqlBuf[1024];
	LookupRet lkRet;
	int i = 0;

	if(insert_flightRoot){
	   for(tmp=Minimum(insert_flightRoot);tmp;tmp=Successor(tmp)){
          cstrlPtr = (CsTravelData *) getTreeObject(tmp);
          sprintf(sqlBuf, "insert into cs_travel_flights values( %d, '%s', %d, %d, '%s', '%s', %d)",
			              cstrlPtr->crewID,
						  "CLOSED",
						  cstrlPtr->dpt_aptID,
						  cstrlPtr->arr_aptID,
						  dt_DateTimeToDateTimeString(cstrlPtr->travel_dptTm, opbuf1, "%Y/%m/%d %H:%M"),
						  dt_DateTimeToDateTimeString(cstrlPtr->travel_arrTm, opbuf2, "%Y/%m/%d %H:%M"),
						  cstrlPtr->rqtID);
		  
		  if(!(myDoWrite(conn,sqlBuf))) {
			logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
			writeWarningData(myconn); exit(1);
		  }
	   }
	}

	if((cstrlBuf = (CsTravelData *) calloc((size_t) 1, sizeof(CsTravelData))) == NULL) {   
			   logMsg(logFile,"%s Line %d, Out of Memory in updateOneTravelRecord().\n", __FILE__, __LINE__);
			   writeWarningData(myconn); exit(1);
	}

	if(concurr_flightRoot){
		for(i=0; i< concurr_crewnumbereffected;i++){
			 memset(cstrlBuf, '\0', sizeof(CsTravelData));
			 cstrlBuf->crewID = concurr_crewListEffected[i];
			 lkRet = Lookup(concurr_flightRoot, cstrlBuf, travelFlightCmp, &tmp);
			 switch(lkRet) {
			 case EmptyTree:
			 case GreaterThanLastItem:
			 case ExactMatchFound: // impossible, since we just entered crewid
				 //It is possible, no travel data.
				 logMsg(logFile,"%s Line %d: crewid %d not found in existing_travel_flights data. lkRet=%d\n", __FILE__,__LINE__, cstrlBuf->crewID, lkRet);
				 break;
			 case NotFoundReturnedNextItem: 
			     for(; tmp; tmp = Successor(tmp)){
                     cstrlPtr = (CsTravelData *) getTreeObject(tmp);
			         if(cstrlPtr->crewID != cstrlBuf->crewID)
                        break;
					 if(cstrlPtr->writtentotable)
					    continue;
					 if(cstrlPtr->inserted){
                          sprintf(sqlBuf, "insert into cs_travel_flights values( %d, '%s', %d, %d, '%s', '%s', %d)",
			                cstrlPtr->crewID,
						    "CLOSED",
						    cstrlPtr->dpt_aptID,
						    cstrlPtr->arr_aptID,
						    dt_DateTimeToDateTimeString(cstrlPtr->travel_dptTm, opbuf1, "%Y/%m/%d %H:%M"),
						    dt_DateTimeToDateTimeString(cstrlPtr->travel_arrTm, opbuf2, "%Y/%m/%d %H:%M"),
						    cstrlPtr->rqtID);
		                 if(!(myDoWrite(conn,sqlBuf))) {
			               logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
			               writeWarningData(myconn); exit(1);
		                 }
                         cstrlPtr->writtentotable =1;
					 }
					 else if(cstrlPtr->updated){
						  sprintf(sqlBuf, "delete from cs_travel_flights where request_id = %d", cstrlPtr->pre_rqtID);
						  if(!(myDoWrite(conn,sqlBuf))) {
			                 logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
			                 writeWarningData(myconn); exit(1);
		                  }
                          sprintf(sqlBuf, "insert into cs_travel_flights values( %d, '%s', %d, %d, '%s', '%s', %d)",
			                 cstrlPtr->crewID,
						     "CLOSED",
						     cstrlPtr->dpt_aptID,
						     cstrlPtr->arr_aptID,
						     dt_DateTimeToDateTimeString(cstrlPtr->travel_dptTm, opbuf1, "%Y/%m/%d %H:%M"),
						     dt_DateTimeToDateTimeString(cstrlPtr->travel_arrTm, opbuf2, "%Y/%m/%d %H:%M"),
						     cstrlPtr->rqtID);
		                 if(!(myDoWrite(conn,sqlBuf))) {
			                 logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,conn->my_errno, conn->my_error_msg);
			                 writeWarningData(myconn); exit(1);
		                 }
						 cstrlPtr->writtentotable =1;
					 }
				 }
				 break;
			 }
		}
	}


}


/*void adjOverlapedCrewAssignments(MY_CONNECTION *myconn, int scenarioid, time_t plwinstarttime)
{ 
  char crewassgSQL1[1024], crewassgSQL2[1024];
  char opbuf1[128],opbuf2[128]; 
  char sqlBuf[1024], sqlBuf1[512], sqlBuf2[512];
  DateTime winstartTm;

  winstartTm = dt_time_tToDateTime(plwinstarttime);

  sprintf(crewassgSQL1, "create temporary table if not exists tmp_crewassignment (crewassignmentid int(11) NOT NULL,\n\
  crewid int(11) NOT NULL,\n\
  fileas varchar(123) NOT NULL,\n\
  aircraftid int(11) NOT NULL,\n\
  position int(11) NOT NULL,\n\
  starttime varchar(20) NOT NULL,\n\
  endtime varchar(20) NOT NULL,\n\
  scenarioid int(11) NOT NULL,\n\
  PRIMARY KEY  (crewassignmentid))");

  if(!myDoWrite(myconn,crewassgSQL1)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }

  strcpy(sqlBuf,"insert into tmp_crewassignment (crewassignmentid, crewid, fileas, aircraftid, position, starttime, endtime, scenarioid)\n");
  sprintf(sqlBuf1, "select distinct a.* from crewassignment a,\n\
                          (select * from proposed_crewassignment where scenarioid = %d ) b\n\
				    where a.aircraftid = b.aircraftid and a.starttime <'%s' and b.starttime >='%s'\n", 
					scenarioid,
					dt_DateTimeToDateTimeString(winstartTm, opbuf1, "%Y/%m/%d %H:%M"),
                    dt_DateTimeToDateTimeString(winstartTm, opbuf2, "%Y/%m/%d %H:%M"));
  strcat(sqlBuf, sqlBuf1);
  strcpy(sqlBuf2, " and a.starttime<=  date_format(b.endtime, '%Y/%m/%d %H:%i') and a.endtime >= date_format(b.starttime, '%Y/%m/%d %H:%i')");
  strcat(sqlBuf, sqlBuf2);

  if(!(myDoWrite(myconn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
  }

  if(!myDoWrite(myconn,"delete from crewassignment where crewassignmentid in (select crewassignmentid from tmp_crewassignment)")) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }

  sprintf(sqlBuf,"update tmp_crewassignment set endtime = date_format(date_sub('%s',", dt_DateTimeToDateTimeString(winstartTm, opbuf1, "%Y/%m/%d %H:%M"));
  strcpy(sqlBuf1," interval 1 hour), '%Y/%m/%d %H:%i')");
  strcat(sqlBuf, sqlBuf1);

  if(!(myDoWrite(myconn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
  }

  if(!(myDoWrite(myconn,"insert into crewassignment (crewassignmentid, crewid, fileas, aircraftid, position, starttime, endtime, scenarioid) select * from tmp_crewassignment"))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
  }

  if(!(myDoWrite(myconn,"delete from tmp_crewassignment"))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }

  //to save some existing crewassignment within planning window
   
   sprintf(crewassgSQL2, "create temporary table if not exists tmp_crewassignment2 (crewassignmentid int(11) NOT NULL,\n\
  crewid int(11) NOT NULL,\n\
  fileas varchar(123) NOT NULL,\n\
  aircraftid int(11) NOT NULL,\n\
  position int(11) NOT NULL,\n\
  starttime varchar(20) NOT NULL,\n\
  endtime varchar(20) NOT NULL,\n\
  scenarioid int(11) NOT NULL,\n\
  PRIMARY KEY  (crewassignmentid))");

  if(!myDoWrite(myconn,crewassgSQL2)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }

  strcpy(sqlBuf,"insert into tmp_crewassignment2 (crewassignmentid, crewid, fileas, aircraftid, position, starttime, endtime, scenarioid)\n");
  sprintf(sqlBuf1, "select distinct a.* from crewassignment a,\n\
                          (select * from proposed_crewassignment where scenarioid = %d ) b\n\
				    where a.aircraftid = b.aircraftid and a.starttime >='%s' and b.starttime >='%s'\n", 
					scenarioid,
					dt_DateTimeToDateTimeString(winstartTm, opbuf1, "%Y/%m/%d %H:%M"),
                    dt_DateTimeToDateTimeString(winstartTm, opbuf2, "%Y/%m/%d %H:%M"));
  strcat(sqlBuf, sqlBuf1);
  strcpy(sqlBuf2, " and a.starttime<=  date_format(b.endtime, '%Y/%m/%d %H:%i') and a.endtime >= date_format(b.starttime, '%Y/%m/%d %H:%i')");
  strcat(sqlBuf, sqlBuf2);

  if(!(myDoWrite(myconn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
  }

  if(!myDoWrite(myconn,"delete from crewassignment where crewassignmentid in (select crewassignmentid from tmp_crewassignment2)")) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }

  if(!(myDoWrite(myconn,"delete from tmp_crewassignment2"))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }
  
}*/



void adjOverlapedCrewAssignments(MY_CONNECTION *myconn, int scenarioid, time_t plwinstarttime)
{ 
  char crewassgSQL1[1024], crewassgSQL2[1024],crewassgSQL3[1024];
  char opbuf1[128],opbuf2[128]; 
  char sqlBuf[1024], sqlBuf1[512], sqlBuf11[512], sqlBuf2[512];
  DateTime winstartTm;

  winstartTm = dt_time_tToDateTime(plwinstarttime);

  sprintf(crewassgSQL1, "create temporary table if not exists tmp_crewassignment (crewassignmentid int(11) NOT NULL,\n\
  crewid int(11) NOT NULL,\n\
  fileas varchar(123) NOT NULL,\n\
  aircraftid int(11) NOT NULL,\n\
  position int(11) NOT NULL,\n\
  starttime varchar(20) NOT NULL,\n\
  endtime varchar(20) NOT NULL,\n\
  scenarioid int(11) NOT NULL,\n\
  PRIMARY KEY  (crewassignmentid))");

  if(!myDoWrite(myconn,crewassgSQL1)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }

  strcpy(sqlBuf,"insert into tmp_crewassignment (crewassignmentid, crewid, fileas, aircraftid, position, starttime, endtime, scenarioid)\n");
  sprintf(sqlBuf1, "select distinct a.* from crewassignment a,\n\
                          (select * from proposed_crewassignment where scenarioid = %d ) b\n\
				    where a.aircraftid = b.aircraftid and a.crewid != b.crewid and a.starttime <'%s'\n", 
					scenarioid,
					dt_DateTimeToDateTimeString(winstartTm, opbuf1, "%Y/%m/%d %H:%M"),
                    dt_DateTimeToDateTimeString(winstartTm, opbuf2, "%Y/%m/%d %H:%M"));
  strcat(sqlBuf, sqlBuf1);
  strcpy(sqlBuf2, " and a.starttime<=  date_format(b.endtime, '%Y/%m/%d %H:%i') and a.endtime >= date_format(b.starttime, '%Y/%m/%d %H:%i')");
  strcat(sqlBuf, sqlBuf2);

  if(!(myDoWrite(myconn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
  }

  if(!myDoWrite(myconn,"delete from crewassignment where crewassignmentid in (select crewassignmentid from tmp_crewassignment)")) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }

  sprintf(sqlBuf,"update tmp_crewassignment set endtime = date_format(date_sub('%s',", dt_DateTimeToDateTimeString(winstartTm, opbuf1, "%Y/%m/%d %H:%M"));
  strcpy(sqlBuf1," interval 1 hour), '%Y/%m/%d %H:%i')");
  strcat(sqlBuf, sqlBuf1);

  if(!(myDoWrite(myconn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
  }

  if(!(myDoWrite(myconn,"insert into crewassignment (crewassignmentid, crewid, fileas, aircraftid, position, starttime, endtime, scenarioid) select * from tmp_crewassignment"))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
  }

  if(!(myDoWrite(myconn,"delete from tmp_crewassignment"))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }

  //to save some existing crewassignment within planning window
   
   sprintf(crewassgSQL2, "create temporary table if not exists tmp_crewassignment2 (crewassignmentid int(11) NOT NULL,\n\
  crewid int(11) NOT NULL,\n\
  fileas varchar(123) NOT NULL,\n\
  aircraftid int(11) NOT NULL,\n\
  position int(11) NOT NULL,\n\
  starttime varchar(20) NOT NULL,\n\
  endtime varchar(20) NOT NULL,\n\
  scenarioid int(11) NOT NULL,\n\
  PRIMARY KEY  (crewassignmentid))");

  if(!myDoWrite(myconn,crewassgSQL2)) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }

  strcpy(sqlBuf,"insert into tmp_crewassignment2 (crewassignmentid, crewid, fileas, aircraftid, position, starttime, endtime, scenarioid)\n");
  sprintf(sqlBuf1, "select distinct a.* from crewassignment a,\n\
                          (select * from proposed_crewassignment where scenarioid = %d ) b\n\
				    where a.aircraftid = b.aircraftid and a.crewid = b.crewid and a.starttime <'%s'\n", 
					scenarioid,
					dt_DateTimeToDateTimeString(winstartTm, opbuf1, "%Y/%m/%d %H:%M"),
                    dt_DateTimeToDateTimeString(winstartTm, opbuf2, "%Y/%m/%d %H:%M"));
  strcat(sqlBuf, sqlBuf1);
  strcpy(sqlBuf2, " and a.starttime<=  date_format(b.endtime, '%Y/%m/%d %H:%i') and a.endtime >= date_format(b.starttime, '%Y/%m/%d %H:%i')");
  strcat(sqlBuf, sqlBuf2);

   if(!(myDoWrite(myconn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
  }

  if(!myDoWrite(myconn,"delete from crewassignment where crewassignmentid in (select crewassignmentid from tmp_crewassignment2)")) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }

  if(!(myDoWrite(myconn,"delete from tmp_crewassignment2"))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		exit(1);
   }

  strcpy(sqlBuf,"insert into crewassignment (crewassignmentid, crewid, aircraftid, position, starttime, endtime, scenarioid)\n");
  strcpy(sqlBuf1, "select distinct b.crewassignmentid, b.crewid, b.aircraftid, b.position, date_format(b.starttime, '%Y/%m/%d %H:%i') as starttime, date_format(b.endtime, '%Y/%m/%d %H:%i'), 4309\n");
  sprintf(sqlBuf11,"from crewassignment a,\n\
                          (select * from proposed_crewassignment where scenarioid = %d ) b\n\
				    where a.aircraftid = b.aircraftid and a.crewid = b.crewid and a.starttime <'%s'\n", 
					scenarioid,
					dt_DateTimeToDateTimeString(winstartTm, opbuf1, "%Y/%m/%d %H:%M"));
  strcat(sqlBuf1, sqlBuf11);
  strcat(sqlBuf, sqlBuf1);
  strcpy(sqlBuf2, " and a.starttime<=  date_format(b.endtime, '%Y/%m/%d %H:%i') and a.endtime >= date_format(b.starttime, '%Y/%m/%d %H:%i')");
  strcat(sqlBuf, sqlBuf2);

   if(!(myDoWrite(myconn,sqlBuf))) {
		logMsg(logFile,"%s Line %d: db errno: %d: %s\n", __FILE__,__LINE__,myconn->my_errno, myconn->my_error_msg);
		writeWarningData(myconn); exit(1);
  }
  
}