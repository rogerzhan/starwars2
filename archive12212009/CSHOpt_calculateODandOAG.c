#include "os_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <search.h>
#include <string.h>
#include "datetime.h"
#include "my_mysql.h"
#include "params.h"
#include "split.h"
#include "logMsg.h"
#include "localTime.h"
#include "airportLatLon.h"
#include "CSHOpt_struct.h"
#include "CSHOpt_readInput.h"
#include "CSHOpt_processInput.h"
#include "CSHOpt_define.h"
#include "CSHOpt_calculateODandOAG.h"
#include "CSHOpt_buildOagOD.h"
#include "cjoin.h"

extern MY_CONNECTION *myconn;
extern MY_CONNECTION *myconn_oag;
extern FILE *logFile;
extern struct optParameters optParam;
extern ODEntry *oDTable;
extern int numOD;

//Susan Change
extern int numODnoDirects;
extern int numOneStopQueries;
extern int numODnoItins;
extern int totDirects;
extern int totOneStops;
//end Susan Change

typedef struct myOagEntry {
    int connAptID;		//ID of airport where connection is made for one-stop itineraries (null for direct itineraries)
    time_t dptTm;
    time_t arrTm;
    //int unAvail;   //= 1 if itinerary is unavailable per Travel Dept
    char *flight_desig;		// flight carrier  // Jintao's change
    int flight_number;
} myOagEntry;

typedef enum { fieldindex_flight_no = 0, fieldindex_flight_carrier, fieldindex_orgAptID, fieldindex_dptTime,
    fieldindex_destAptID, fieldindex_arrTime
} OAG_Direct_Columns;

typedef enum { fieldindex_leg1OrgAptID = 0, fieldindex_leg1DestAptID, fieldindex_leg1Flight_no, fieldindex_leg1Flight_carrier, fieldindex_leg1DptTime, fieldindex_leg1ArrTime,
    fieldindex_leg2OrgAptID, fieldindex_leg2DestAptID, fieldindex_leg2Flight_no, fieldindex_leg2Flight_carrier, fieldindex_leg2DptTime, fieldindex_leg2ArrTime
} OAG_Onestop_Columns;

static int oagCompare(void *a, void *b);


void updatesODOAG_earlydpt(ODEntry * oDEnt, ODEntry * exgOD, OagEntry * oagList, int *numOag)
{
    char OAG1ItinerarySQL[3072];
    myOagEntry *oagptr, *oagptrbuf;
    int commOrAptID;
    int commDestAptID;
//    time_t earlyDpt;
    time_t new_earlyDpt;
    time_t exg_earlyDpt;
//    time_t bound_earlyDpt;
//    DateTime DateTime_earlyDpt;
    DateTime DateTime_DptTm;
    DateTime DateTime_new_earlyDpt;
    DateTime DateTime_exg_earlyDpt;
//    DateTime DateTime_bound_earlyDpt;
//    time_t lateArr;
//    time_t new_lateArr;
//    time_t exg_lateArr;
    time_t bound_lateArr;
//    DateTime DateTime_lateArr;
    DateTime DateTime_ArrTm;
//    DateTime DateTime_new_lateArr;
//    DateTime DateTime_exg_lateArr;
    DateTime DateTime_bound_lateArr;
//    double cost;
    int errNbr;
    int k;
//    int total_direct;
    char tbuf1[32], tbuf2[32], tbuf3[32];
    char oag1sql[1024], oag2sql[1024];
    BINTREENODE *resultRoot = NULL;
    CjoinResult *resultPtr;
    int cjoin_return;
    BINTREENODE *oagRoot = NULL;
    BINTREENODE *tmp;
    MYSQL_RES *res;
    MYSQL_FIELD *cols;
    MYSQL_ROW row;
    my_ulonglong rowCount;
//Susan Change
    int hours;
//End Susan Change

    commOrAptID = oDEnt->commOrAptID;
    commDestAptID = oDEnt->commDestAptID;
    new_earlyDpt = oDEnt->earlyDpt;
    exg_earlyDpt = exgOD->earlyDpt;
    DateTime_new_earlyDpt = dt_time_tToDateTime(new_earlyDpt);
    DateTime_exg_earlyDpt = dt_time_tToDateTime(exg_earlyDpt);
    bound_lateArr = exgOD->lateArr;
    DateTime_bound_lateArr = dt_time_tToDateTime(bound_lateArr);
    sprintf(OAG1ItinerarySQL, "\
select\n\
	FD_flight_number,\n\
	FD_airline_desig,\n\
	dep_aptid,\n\
	UTC_departure_time,\n\
	arr_aptid,\n\
	UTC_arrival_time\n\
from\n\
	ssim\n\
where\n\
	dep_aptid=%d and\n\
	arr_aptid=%d and\n\
	UTC_departure_time >='%s' and\n\
	UTC_departure_time < '%s' and\n\
	UTC_arrival_time <= '%s'",
    commOrAptID,
    commDestAptID,
    dt_DateTimeToDateTimeString(DateTime_new_earlyDpt, tbuf1, "%Y/%m/%d %H:%M"),
    dt_DateTimeToDateTimeString(DateTime_exg_earlyDpt, tbuf2, "%Y/%m/%d %H:%M"),
    dt_DateTimeToDateTimeString(DateTime_bound_lateArr, tbuf3, "%Y/%m/%d %H:%M"));


    if (!myDoQuery(myconn_oag, OAG1ItinerarySQL, &res, &cols)) {
	logMsg(logFile, "%s Line %d: db errno: %d: %s\n", __FILE__, __LINE__, myconn_oag->my_errno, myconn_oag->my_error_msg);
	exit(1);
    }
    rowCount = mysql_num_rows(res);
    //Susan Change
    if (!rowCount)
	numODnoDirects++;
    //end Susan Change

    for (k = 0; k < rowCount; k++) {
	row = mysql_fetch_row(res);
	if (!row)
	    break;
	if (!row[fieldindex_flight_no])
	    continue;
	oagptr = (myOagEntry *) calloc((size_t) 1, sizeof(myOagEntry));
	if (!oagptr) {
	    logMsg(logFile, "%s Line %d, Out of memory in updatesODOAG_earlydpt().\n", __FILE__, __LINE__);
	    exit(1);
	}
	oagptr->connAptID = 0;
	//oagptr->unAvail=0;
	oagptr->flight_desig = strdup(row[fieldindex_flight_carrier]);
	oagptr->flight_number = atoi(row[fieldindex_flight_no]);
	if ((DateTime_DptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[fieldindex_dptTime], NULL, &errNbr)) == BadDateTime) {
	    logMsg(logFile, "%s Line %d, Bad date in updatesODOAG_earlydpt(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[fieldindex_dptTime]);
	    exit(1);
	}
	oagptr->dptTm = DateTimeToTime_t(DateTime_DptTm);
	if ((DateTime_ArrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[fieldindex_arrTime], NULL, &errNbr)) == BadDateTime) {
	    logMsg(logFile, "%s Line %d, Bad date in updatesODOAG_earlydpt(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[fieldindex_arrTime]);
	    exit(1);
	}
	oagptr->arrTm = DateTimeToTime_t(DateTime_ArrTm);
	if (!(TreeSearch(oagRoot, oagptr, oagCompare))) {
	    if (!(oagRoot = RBTreeInsert(oagRoot, oagptr, oagCompare))) {
		logMsg(logFile, "%s Line %d, RBTreeInsert() failed in updatesODOAG_earlydpt().\n", __FILE__, __LINE__);
		exit(1);
	    }
	    (*numOag)++;
	}
    }
    mysql_free_result(res);

    //Susan Change
    totDirects += (*numOag);
    hours = difftime(exgOD->earlyDpt, oDEnt->earlyDpt) / 3600;

    //if there are not enough direct itineraries, 
    if ((*numOag) < ((hours - (int) (hours / 24) * 8) * MIN_DIRECTS_PER_HOUR)) {
	numOneStopQueries++;
	//end Susan Change
	//if there are not enough direct itineraries,
	//if(*numOag < (difftime(exgOD->earlyDpt, oDEnt->earlyDpt)*MIN_DIRECTS_PER_HOUR)/3600)
	//Jintao's change
	//Begins
	//CitationShares TO DO: query OAG2 for single-stop itineraries and add to oagList.  update numOAG.
	// winstart=dt_time_tToDateTime(optParam.windowStart);  
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
    dep_aptid=%d and\n\
    UTC_departure_time >='%s' and\n\
    UTC_departure_time < '%s' and\n\
    UTC_arrival_time <='%s'",
	commOrAptID,
	dt_DateTimeToDateTimeString(DateTime_new_earlyDpt, tbuf1, "%Y/%m/%d %H:%M"),
	dt_DateTimeToDateTimeString(DateTime_exg_earlyDpt, tbuf2, "%Y/%m/%d %H:%M"),
	dt_DateTimeToDateTimeString(
	    dt_addToDateTime(Minutes, -(2*MIN_LAYOVER) / 60, DateTime_bound_lateArr),
	    tbuf3, "%Y/%m/%d %H:%M")
	);

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
	arr_aptid=%d and\n\
	UTC_arrival_time >='%s' and\n\
	UTC_arrival_time <='%s'",
	    commDestAptID,
	    dt_DateTimeToDateTimeString(
		dt_addToDateTime(Minutes, (2* MIN_LAYOVER) / 60, DateTime_new_earlyDpt),
		tbuf1, "%Y/%m/%d %H:%M"),
	    dt_DateTimeToDateTimeString(DateTime_bound_lateArr, tbuf2, "%Y/%m/%d %H:%M")
	);

	cjoin_return = cjoin(myconn_oag, &resultRoot, oag1sql, oag2sql, MIN_LAYOVER, MAX_LAYOVER);

	if (cjoin_return) {
	    for (tmp = Minimum(resultRoot); tmp; tmp = Successor(tmp)) {

		resultPtr = (CjoinResult *) getTreeObject(tmp);
		if (!resultPtr->leg1Flight_no[0])
		    continue;
		oagptr = (myOagEntry *) calloc((size_t) 1, sizeof(myOagEntry));
		if (!oagptr) {
		    logMsg(logFile, "%s Line %d, Out of memory in updatesODOAG_earlydpt().\n", __FILE__, __LINE__);
		    exit(1);
		}
		oagptr->connAptID = resultPtr->leg1DestAptID;
		//oagptr->unAvail=0;
		oagptr->flight_desig = strdup(resultPtr->leg1Flight_carrier);
		oagptr->flight_number = atoi(resultPtr->leg1Flight_no) * 10000 + atoi(resultPtr->leg2Flight_no);
		oagptr->dptTm = DateTimeToTime_t(resultPtr->leg1DptTime);
		oagptr->arrTm = DateTimeToTime_t(resultPtr->leg2ArrTime);
		if (!(TreeSearch(oagRoot, oagptr, oagCompare))) {
		    if (!(oagRoot = RBTreeInsert(oagRoot, oagptr, oagCompare))) {
			logMsg(logFile, "%s Line %d, RBTreeInsert() failed in updatesODOAG_earlydpt().\n", __FILE__, __LINE__);
			exit(1);
		    }
		    (*numOag)++;
		    //Susan Change
		    totOneStops++;
		    //End Susan Change
		}
	    }
	    while (resultRoot) {
		tmp = Minimum(resultRoot);
		resultPtr = (CjoinResult *) getTreeObject(tmp);
		resultRoot = RBTreeDelete(resultRoot, tmp);
		free(resultPtr);
	    }
	}
    }
    //sort oagList primarily by increasing dptTm (for searching and for pruning), 
    //and secondarily by decreasing arrTm (to make pruning efficient)
    //note: we may want to move prune(below)INTO the sort if it is more efficient to do so
    if (oagRoot) {
	for (tmp = Minimum(oagRoot), k = 0; tmp && k < MAX_OAG_PER_OD; tmp = Successor(tmp), k++) {
	    oagptrbuf = (myOagEntry *) getTreeObject(tmp);
	    oagList[k].connAptID = oagptrbuf->connAptID;
	    //oagList[k].unAvail=oagptrbuf->unAvail;
	    oagList[k].dptTm = oagptrbuf->dptTm;
	    oagList[k].arrTm = oagptrbuf->arrTm;
	}
    }
    // Don't free like this:
    // free(oagRoot);
    // oagRoot = NULL;
    //
    // Free like this:
    while (oagRoot) {
	tmp = Minimum(oagRoot);
	oagptr = (myOagEntry *) getTreeObject(tmp);
	oagRoot = RBTreeDelete(oagRoot, tmp);
	free(oagptr);
    }
}


void updatesODOAG_latearr(ODEntry * oDEnt, ODEntry * exgOD, OagEntry * oagList, int *numOag)
{
    char OAG1ItinerarySQL[3072];
    myOagEntry *oagptr, *oagptrbuf;
    int commOrAptID;
    int commDestAptID;
//    time_t earlyDpt;
//    time_t new_earlyDpt;
//    time_t exg_earlyDpt;
    time_t bound_earlyDpt;
//    DateTime DateTime_earlyDpt;
    DateTime DateTime_DptTm;
//    DateTime DateTime_new_earlyDpt;
//    DateTime DateTime_exg_earlyDpt;
    DateTime DateTime_bound_earlyDpt;
//    time_t lateArr;
    time_t new_lateArr;
    time_t exg_lateArr;
//    time_t bound_lateArr;
//    DateTime DateTime_lateArr;
    DateTime DateTime_ArrTm;
    DateTime DateTime_new_lateArr;
    DateTime DateTime_exg_lateArr;
//    DateTime DateTime_bound_lateArr;
//    DateTime query_start;
//    DateTime query_end;
//    double cost;
    int errNbr;
    int k;
//    int total_direct;
    char tbuf1[32], tbuf2[32], tbuf3[32];
    char oag1sql[1024], oag2sql[1024];
    BINTREENODE *resultRoot = NULL;
    CjoinResult *resultPtr;
    int cjoin_return;
    BINTREENODE *oagRoot = NULL;
    BINTREENODE *tmp;
    MYSQL_RES *res;
    MYSQL_FIELD *cols;
    MYSQL_ROW row;
    my_ulonglong rowCount;
//Susan Change
    int hours;
//End Susan Change


    commOrAptID = oDEnt->commOrAptID;
    commDestAptID = oDEnt->commDestAptID;
    new_lateArr = oDEnt->lateArr;
    exg_lateArr = exgOD->lateArr;
    DateTime_new_lateArr = dt_time_tToDateTime(new_lateArr);
    DateTime_exg_lateArr = dt_time_tToDateTime(exg_lateArr);
    bound_earlyDpt = exgOD->earlyDpt;
    DateTime_bound_earlyDpt = dt_time_tToDateTime(bound_earlyDpt);

    sprintf(OAG1ItinerarySQL, "\
select\n\
	FD_flight_number,\n\
	FD_airline_desig,\n\
	dep_aptid,\n\
	UTC_departure_time,\n\
	arr_aptid,\n\
	UTC_arrival_time\n\
from\n\
	ssim\n\
where\n\
	dep_aptid=%d and\n\
	arr_aptid=%d and\n\
	UTC_arrival_time >'%s' and\n\
	UTC_arrival_time <= '%s' and\n\
	UTC_departure_time>='%s'", commOrAptID, commDestAptID, dt_DateTimeToDateTimeString(DateTime_exg_lateArr, tbuf1, "%Y/%m/%d %H:%M"), dt_DateTimeToDateTimeString(DateTime_new_lateArr, tbuf2, "%Y/%m/%d %H:%M"), dt_DateTimeToDateTimeString(DateTime_bound_earlyDpt, tbuf3, "%Y/%m/%d %H:%M"));

    if (!myDoQuery(myconn_oag, OAG1ItinerarySQL, &res, &cols)) {
	logMsg(logFile, "%s Line %d: db errno: %d: %s\n", __FILE__, __LINE__, myconn_oag->my_errno, myconn_oag->my_error_msg);
	exit(1);
    }
    rowCount = mysql_num_rows(res);
    //Susan Change
    if (!rowCount)
	numODnoDirects++;
    //end Susan Change

    for (k = 0; k < rowCount; k++) {
	row = mysql_fetch_row(res);
	if (!row)
	    break;
	if (!row[fieldindex_flight_no])
	    continue;
	oagptr = (myOagEntry *) calloc((size_t) 1, sizeof(myOagEntry));
	if (!oagptr) {
	    logMsg(logFile, "%s Line %d, Out of memory in updatesODOAG_latearr().\n", __FILE__, __LINE__);
	    exit(1);
	}

	oagptr->connAptID = 0;
	//oagptr->unAvail=0;
	oagptr->flight_desig = strdup(row[fieldindex_flight_carrier]);
	oagptr->flight_number = atoi(row[fieldindex_flight_no]);
	if ((DateTime_DptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[fieldindex_dptTime], NULL, &errNbr)) == BadDateTime) {
	    logMsg(logFile, "%s Line %d, Bad date in updatesODOAG_latearr(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[fieldindex_dptTime]);
	    exit(1);
	}
	oagptr->dptTm = DateTimeToTime_t(DateTime_DptTm);
	if ((DateTime_ArrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[fieldindex_arrTime], NULL, &errNbr)) == BadDateTime) {
	    logMsg(logFile, "%s Line %d, Bad date in updatesODOAG_latearr(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[fieldindex_arrTime]);
	    exit(1);
	}
	oagptr->arrTm = DateTimeToTime_t(DateTime_ArrTm);
	if (!(TreeSearch(oagRoot, oagptr, oagCompare))) {
	    if (!(oagRoot = RBTreeInsert(oagRoot, oagptr, oagCompare))) {
		logMsg(logFile, "%s Line %d, RBTreeInsert() failed in updatesODOAG_latearr().\n", __FILE__, __LINE__);
		exit(1);
	    }
	    (*numOag)++;
	}
    }
    mysql_free_result(res);

    //Susan Change
    totDirects += (*numOag);
    hours = difftime(oDEnt->lateArr, exgOD->lateArr) / 3600;

    //if there are not enough direct itineraries, 
    if ((*numOag) < ((hours - (int) (hours / 24) * 8) * MIN_DIRECTS_PER_HOUR)) {
	numOneStopQueries++;
	//end Susan Change
	//query OAG2 for single-stop itineraries and add to oagList.  update numOAG.
	//winend=dt_time_tToDateTime(optParam.windowEnd);  

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
    dep_aptid=%d and\n\
    UTC_departure_time >='%s' and\n\
    UTC_departure_time <='%s'",
	commOrAptID,
	dt_DateTimeToDateTimeString(DateTime_bound_earlyDpt, tbuf1, "%Y/%m/%d %H:%M"),
	dt_DateTimeToDateTimeString(
	    dt_addToDateTime(Minutes, -( 2*MIN_LAYOVER) / 60, DateTime_new_lateArr),
	    tbuf3, "%Y/%m/%d %H:%M"));

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
    arr_aptid=%d and\n\
    UTC_arrival_time >'%s' and\n\
    UTC_arrival_time <='%s' and\n\
    UTC_departure_time >='%s'",
	commDestAptID,
	dt_DateTimeToDateTimeString(DateTime_exg_lateArr, tbuf1, "%Y/%m/%d %H:%M"),
	dt_DateTimeToDateTimeString(DateTime_new_lateArr, tbuf2, "%Y/%m/%d %H:%M"),
	dt_DateTimeToDateTimeString(
	    dt_addToDateTime(Minutes, (2*MIN_LAYOVER) / 60, DateTime_bound_earlyDpt),
	    tbuf3, "%Y/%m/%d %H:%M"));

	cjoin_return = cjoin(myconn_oag, &resultRoot, oag1sql, oag2sql, MIN_LAYOVER, MAX_LAYOVER);

	if (cjoin_return) {
	    for (tmp = Minimum(resultRoot); tmp; tmp = Successor(tmp)) {

		resultPtr = (CjoinResult *) getTreeObject(tmp);
		if (!resultPtr->leg1Flight_no[0])
		    continue;
		oagptr = (myOagEntry *) calloc((size_t) 1, sizeof(myOagEntry));
		if (!oagptr) {
		    logMsg(logFile, "%s Line %d, Out of memory in updatesODOAG_earlydpt().\n", __FILE__, __LINE__);
		    exit(1);
		}
		oagptr->connAptID = resultPtr->leg1DestAptID;
		//oagptr->unAvail=0;
		oagptr->flight_desig = strdup(resultPtr->leg1Flight_carrier);
		oagptr->flight_number = atoi(resultPtr->leg1Flight_no) * 10000 + atoi(resultPtr->leg2Flight_no);
		oagptr->dptTm = DateTimeToTime_t(resultPtr->leg1DptTime);
		oagptr->arrTm = DateTimeToTime_t(resultPtr->leg2ArrTime);
		if (!(TreeSearch(oagRoot, oagptr, oagCompare))) {
		    if (!(oagRoot = RBTreeInsert(oagRoot, oagptr, oagCompare))) {
			logMsg(logFile, "%s Line %d, RBTreeInsert() failed in updatesODOAG_earlydpt().\n", __FILE__, __LINE__);
			exit(1);
		    }
		    (*numOag)++;
		}
	    }
	    while (resultRoot) {
		tmp = Minimum(resultRoot);
		resultPtr = (CjoinResult *) getTreeObject(tmp);
		resultRoot = RBTreeDelete(resultRoot, tmp);
		free(resultPtr);
	    }
	}
    }
    //sort oagList primarily by increasing dptTm (for searching and for pruning), 
    //and secondarily by decreasing arrTm (to make pruning efficient)
    //note: we may want to move prune(below)INTO the sort if it is more efficient to do so
    if (oagRoot) {
	for (tmp = Minimum(oagRoot), k = 0; tmp && k < MAX_OAG_PER_OD; tmp = Successor(tmp), k++) {
	    oagptrbuf = (myOagEntry *) getTreeObject(tmp);
	    oagList[k].connAptID = oagptrbuf->connAptID;
	    //oagList[k].unAvail=oagptrbuf->unAvail;
	    oagList[k].dptTm = oagptrbuf->dptTm;
	    oagList[k].arrTm = oagptrbuf->arrTm;
	}
    }
    // Don't free like this:
    // free(oagRoot);
    // oagRoot = NULL;
    //
    // Free like this:
    while (oagRoot) {
	tmp = Minimum(oagRoot);
	oagptr = (myOagEntry *) getTreeObject(tmp);
	oagRoot = RBTreeDelete(oagRoot, tmp);
	free(oagptr);
    }
}

void getNewODOAG(ODEntry * oDEnt, OagEntry * oagList, int *numOag)
{				//Jintao's change, begins
    char OAG1ItinerarySQL[3072];
    myOagEntry *oagptr, *oagptrbuf;
    int commOrAptID;
    int commDestAptID;
    time_t earlyDpt;
    DateTime DateTime_earlyDpt;
    DateTime DateTime_DptTm;
    time_t lateArr;
    DateTime DateTime_lateArr;
    DateTime DateTime_ArrTm;
    DateTime query_start;
    DateTime query_end;
    double cost;
    //char *OAGListSQL;
    int errNbr;
    int i, k;
//    int total_direct;
    char tbuf1[32], tbuf2[32];
    char oag1sql[1024], oag2sql[1024];
    BINTREENODE *resultRoot = NULL;
    CjoinResult *resultPtr;
    int cjoin_return;
    MYSQL_RES *res;
    MYSQL_FIELD *cols;
    MYSQL_ROW row;
    my_ulonglong rowCount;
    BINTREENODE *oagRoot = NULL;
    BINTREENODE *tmp;
    //ends

//Susan Change
    int hours;
//End Susan Change

    commOrAptID = oDEnt->commOrAptID;
    commDestAptID = oDEnt->commDestAptID;
    earlyDpt = oDEnt->earlyDpt;
    lateArr = oDEnt->lateArr;
    cost = oDEnt->cost;
    DateTime_earlyDpt = dt_time_tToDateTime(earlyDpt);
    DateTime_lateArr = dt_time_tToDateTime(lateArr);


    sprintf(OAG1ItinerarySQL, "\
select\n\
	FD_flight_number,\n\
	FD_airline_desig,\n\
	dep_aptid,\n\
	UTC_departure_time,\n\
	arr_aptid,\n\
	UTC_arrival_time\n\
from\n\
	ssim\n\
where\n\
	dep_aptid=%d and\n\
	arr_aptid=%d and\n\
	UTC_departure_time >='%s' and\n\
	UTC_arrival_time<='%s'\n",
	commOrAptID,
	commDestAptID,
	dt_DateTimeToDateTimeString(DateTime_earlyDpt, tbuf1, "%Y/%m/%d %H:%M"),
	dt_DateTimeToDateTimeString(DateTime_lateArr, tbuf2, "%Y/%m/%d %H:%M"));

    if (!myDoQuery(myconn_oag, OAG1ItinerarySQL, &res, &cols)) {
	logMsg(logFile, "%s Line %d: db errno: %d: %s\n", __FILE__, __LINE__, myconn_oag->my_errno, myconn_oag->my_error_msg);
	exit(1);
    }
    rowCount = mysql_num_rows(res);
    if (!rowCount) {
	//Susan Change
	numODnoDirects++;
	//end Susan Change
    }

    for (i = 0; i < rowCount; i++) {
	row = mysql_fetch_row(res);
	if (!row)
	    break;
	if (!row[fieldindex_flight_no])
	    continue;
	oagptr = (myOagEntry *) calloc((size_t) 1, sizeof(myOagEntry));
	if (!oagptr) {
	    logMsg(logFile, "%s Line %d, Out of memory in getNewODOAG().\n", __FILE__, __LINE__);
	    exit(1);
	}
	oagptr->connAptID = 0;
	//oagptr->unAvail=0;
	oagptr->flight_desig = strdup(row[fieldindex_flight_carrier]);
	oagptr->flight_number = atoi(row[fieldindex_flight_no]);
	if ((DateTime_DptTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[fieldindex_dptTime], NULL, &errNbr)) == BadDateTime) {
	    logMsg(logFile, "%s Line %d, Bad date in getNewODOAG(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[fieldindex_dptTime]);
	    exit(1);
	}
	oagptr->dptTm = DateTimeToTime_t(DateTime_DptTm);
	if ((DateTime_ArrTm = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[fieldindex_arrTime], NULL, &errNbr)) == BadDateTime) {
	    logMsg(logFile, "%s Line %d, Bad date in getNewODOAG(). errNbr=%d, date string=%s\n", __FILE__, __LINE__, errNbr, row[fieldindex_arrTime]);
	    exit(1);
	}
	oagptr->arrTm = DateTimeToTime_t(DateTime_ArrTm);
	if (!(TreeSearch(oagRoot, oagptr, oagCompare))) {
	    if (!(oagRoot = RBTreeInsert(oagRoot, oagptr, oagCompare))) {
		logMsg(logFile, "%s Line %d, RBTreeInsert() failed in getNewODOAG().\n", __FILE__, __LINE__);
		exit(1);
	    }
	    (*numOag)++;
	}
    }
    mysql_free_result(res);
//Susan Change
    totDirects += (*numOag);
    hours = difftime(oDEnt->lateArr, oDEnt->earlyDpt) / 3600;

    //if there are not enough direct itineraries, 
    if ((*numOag) < ((hours - (int) (hours / 24) * 8) * MIN_DIRECTS_PER_HOUR)) {
	numOneStopQueries++;
	//end Susan Change

	//query single-stop itineraries and add to oagList.  update numOAG.
	query_start = dt_addToDateTime(Minutes, (2 * MIN_LAYOVER) / 60, DateTime_earlyDpt);
	query_end = dt_addToDateTime(Minutes, - (2 * MIN_LAYOVER) / 60, DateTime_lateArr);  //RLZ: Assume the two legs take at least MIN_LAYOVER

	sprintf(oag1sql, "\
select distinct\n\
	dep_aptid,\n\
	arr_aptid,\n\
	FD_flight_number,\n\
	FD_airline_desig,\n\
	UTC_departure_time,\n\
	UTC_arrival_time \n\
from\n\
	ssim use index(orig_departure_time)\n\
where\n\
	dep_aptid=%d and\n\
	UTC_departure_time >= '%s' and\n\
	UTC_departure_time <= '%s'",
	commOrAptID,
	dt_DateTimeToDateTimeString(DateTime_earlyDpt, tbuf1, "%Y/%m/%d %H:%M"),
	dt_DateTimeToDateTimeString(query_end, tbuf2, "%Y/%m/%d %H:%M"));

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
	arr_aptid=%d and\n\
	UTC_arrival_time >='%s' and\n\
	UTC_arrival_time <='%s'",
	commDestAptID,
	dt_DateTimeToDateTimeString(query_start, tbuf1, "%Y/%m/%d %H:%M"),
	dt_DateTimeToDateTimeString(DateTime_lateArr, tbuf2, "%Y/%m/%d %H:%M"));

	cjoin_return = cjoin(myconn_oag, &resultRoot, oag1sql, oag2sql, MIN_LAYOVER, MAX_LAYOVER);

	if (cjoin_return) {
	    for (tmp = Minimum(resultRoot); tmp; tmp = Successor(tmp)) {

		resultPtr = (CjoinResult *) getTreeObject(tmp);
		if (!resultPtr->leg1Flight_no[0])
		    continue;
		oagptr = (myOagEntry *) calloc((size_t) 1, sizeof(myOagEntry));
		if (!oagptr) {
		    logMsg(logFile, "%s Line %d, Out of memory in getNewODOAG_earlydpt().\n", __FILE__, __LINE__);
		    exit(1);
		}
		oagptr->connAptID = resultPtr->leg1DestAptID;
		//oagptr->unAvail=0;
		oagptr->flight_desig = strdup(resultPtr->leg1Flight_carrier);
		oagptr->flight_number = atoi(resultPtr->leg1Flight_no) * 10000 + atoi(resultPtr->leg2Flight_no);
		oagptr->dptTm = DateTimeToTime_t(resultPtr->leg1DptTime);
		oagptr->arrTm = DateTimeToTime_t(resultPtr->leg2ArrTime);
		if (!(TreeSearch(oagRoot, oagptr, oagCompare))) {
		    if (!(oagRoot = RBTreeInsert(oagRoot, oagptr, oagCompare))) {
			logMsg(logFile, "%s Line %d, RBTreeInsert() failed in getNewODOAG_earlydpt().\n", __FILE__, __LINE__);
			exit(1);
		    }
		    (*numOag)++;
		    //Susan Change
		    totOneStops++;
		    //End Susan Change
		}
	    }
	    while (resultRoot) {
		tmp = Minimum(resultRoot);
		resultPtr = (CjoinResult *) getTreeObject(tmp);
		resultRoot = RBTreeDelete(resultRoot, tmp);
		free(resultPtr);
	    }
	}
    }
    //sort oagList primarily by increasing dptTm (for searching and for pruning), 
    //and secondarily by decreasing arrTm (to make pruning efficient)
    //note: we may want to move prune(below)INTO the sort if it is more efficient to do so
    if (oagRoot) {
	for (tmp = Minimum(oagRoot), k = 0; tmp && k < MAX_OAG_PER_OD; tmp = Successor(tmp), k++) {
	    oagptrbuf = (myOagEntry *) getTreeObject(tmp);
	    oagList[k].connAptID = oagptrbuf->connAptID;
	    //oagList[k].unAvail=oagptrbuf->unAvail;
	    oagList[k].dptTm = oagptrbuf->dptTm;
	    oagList[k].arrTm = oagptrbuf->arrTm;
	    /*
	       DateTime_DptTm=dt_time_tToDateTime(oagptrbuf->dptTm);
	       DateTime_ArrTm=dt_time_tToDateTime(oagptrbuf->arrTm);
	       printf("%d, %d, %s, %s, %s \n",
	       oagptrbuf->connAptID,
	       oagptrbuf->unAvail,
	       dt_DateTimeToDateTimeString(DateTime_DptTm,tbuf1,"%Y/%m/%d %H:%M"),
	       dt_DateTimeToDateTimeString(DateTime_ArrTm,tbuf2,"%Y/%m/%d %H:%M"),
	       oagptrbuf->flight_desig
	       );  */
	}
    }
    // Don't free like this:
    // free(oagRoot);
    // oagRoot = NULL;
    //
    // Free like this:
    while (oagRoot) {
	tmp = Minimum(oagRoot);
	oagptr = (myOagEntry *) getTreeObject(tmp);
	oagRoot = RBTreeDelete(oagRoot, tmp);
	free(oagptr);
    }
}

//Jintao's change
static int oagCompare(void *a, void *b)
{
    myOagEntry *a1 = (myOagEntry *) a;
    myOagEntry *b1 = (myOagEntry *) b;
    int ret;

    if (a1->dptTm != b1->dptTm)
	return (a1->dptTm - b1->dptTm);
    if (a1->arrTm < b1->arrTm)
	return (1);
    else if (a1->arrTm > b1->arrTm)
	return (-1);
    if ((ret = strcmp(a1->flight_desig, b1->flight_desig)))
	return (ret);
    return (a1->flight_number - b1->flight_number);

}

void writeODTable()
{
    char sqlBuf[512];
    char *sqlTruncate_ODTable = "TRUNCATE TABLE ODTable";
    char *sqlTruncate_OAGTable = "TRUNCATE TABLE OAGTable";
    char tbuf1[32];
    char tbuf2[32];
    char tbuf3[32];
    char tbuf4[32];
    DateTime DateTime_ODearlyDpt;
    DateTime DateTime_ODlateArr;
    DateTime DateTime_OAGDpt;
    DateTime DateTime_OAGArr;
    OagEntry *oagptr;
    int i, k;

	logMsg(logFile,"** Begin of writing data to OD and OAG table.\n\n");

    if (!(myDoWrite(myconn_oag, sqlTruncate_ODTable))) {
	logMsg(logFile, "%s Line %d: db errno: %d: %s\n", __FILE__, __LINE__, myconn->my_errno, myconn->my_error_msg);
	exit(1);
    }
    if (!(myDoWrite(myconn_oag, sqlTruncate_OAGTable))) {
	logMsg(logFile, "%s Line %d: db errno: %d: %s\n", __FILE__, __LINE__, myconn->my_errno, myconn->my_error_msg);
	exit(1);
    }
    
	for (i = 0; i < numOD; i++) {
	DateTime_ODearlyDpt = dt_time_tToDateTime(oDTable[i].earlyDpt);
	DateTime_ODlateArr = dt_time_tToDateTime(oDTable[i].lateArr);
	sprintf(sqlBuf, "insert into ODTable values(%d, %d, %d, '%s', '%s', %f)", i + 1, oDTable[i].commOrAptID, oDTable[i].commDestAptID, dt_DateTimeToDateTimeString(DateTime_ODearlyDpt, tbuf1, "%Y/%m/%d %H:%M"), dt_DateTimeToDateTimeString(DateTime_ODlateArr, tbuf2, "%Y/%m/%d %H:%M"), oDTable[i].cost);
	if (!(myDoWrite(myconn_oag, sqlBuf))) {
	    logMsg(logFile, "%s Line %d: db errno: %d: %s\n", __FILE__, __LINE__, myconn->my_errno, myconn->my_error_msg);
	    exit(1);
	}
	for (k = 0; k < oDTable[i].numOag; k++) {
	    oagptr = &(oDTable[i].oagList[k]);
		DateTime_OAGDpt = dt_time_tToDateTime(oagptr->dptTm);
	    DateTime_OAGArr = dt_time_tToDateTime(oagptr->arrTm);
		sprintf(sqlBuf,
                "insert into OAGTable values(%d, %d, '%s', '%s', %d)",
                i + 1,
                oagptr->connAptID,
                dt_DateTimeToDateTimeString(DateTime_OAGDpt, tbuf3, "%Y/%m/%d %H:%M"),
                dt_DateTimeToDateTimeString(DateTime_OAGArr, tbuf4, "%Y/%m/%d %H:%M"),
                 0);
	    if (!(myDoWrite(myconn_oag, sqlBuf))) {
		logMsg(logFile, "%s Line %d: db errno: %d: %s\n", __FILE__, __LINE__, myconn->my_errno, myconn->my_error_msg);
		exit(1);
	    }
	  }
	}
	logMsg(logFile,"** End of writing data to OD and OAG table.\n\n");
}


// Jintao's changes
//Ends
