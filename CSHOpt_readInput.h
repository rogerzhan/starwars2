// Function declarations
#ifndef CSHOPT_INPUT_INC
#define CSHOPT_INPUT_INC 1
#include "common.h"
#include "bintree.h"
#include "queue.h"
#include "my_mysql.h"//fei
#include "CSHOpt_struct.h"

typedef struct rawaircraftdata {
    char *rowtype;
    int recid;
    int aircraftid;
    char *registration;
    int ac_type;
    int demandid;
    int outaptid;
    char *outicao;
    int inaptid;
    char *inicao;
	int outfboid;
	int infboid;
    DateTime rec_outtime;
    DateTime rec_intime;
    DateTime actualout;
    DateTime actualoff;
    DateTime actualon;
    DateTime actualin;
    int sequenceposition;
    int crewnotified;
    int minutes_since_previous_landing;
	int updatedFlag;
} RawAircraftData;

typedef enum {
    CRWID_crewid = 0,
    CRWID_employeenumber,
    CRWID_baseairportid,
    CRWID_baseicao,
    CRWID_fileas,
    CRWID_nbr_items
} CRWID;

typedef enum {
    CRW_employeenumber = 0,
    CRW_aircraftTypeName,
    CRW_aircraftTypeID,
    CRW_starttime,
    CRW_endtime,
    CRW_crewid,
    CRW_fileas,
    CRW_crewassignmentid,
    CRW_aircraftid,
    CRW_registration,
    CRW_position,
    CRW_scenarioid,
    CRW_nbr_items
} CRW;

typedef enum {
    CS_CRW_crewid = 0,
    CS_CRW_starttime,
    CS_CRW_endtime,
    CS_CRW_ca_aircraftid,
    CS_CRW_cd_aircraftid,
    CS_CRW_ca_registration,
    CS_CRW_cd_registration,
    CS_CRW_position,
    CS_CRW_scheduled_on,
    CS_CRW_scheduled_off,
    CS_CRW_actual_on,
    CS_CRW_actual_off,
    CS_CRW_currdate,
    CS_CRW_lastupdated,
    CS_CRW_scenarioid,
    CS_CRW_nbr_items
} CS_CRW;

typedef enum {
    SS_zbadgeid = 0,
    SS_zdeptdesc,
    SS_dtdate,
    SS_zlname,
    SS_zfname,
    SS_zmname,
    SS_lempid,
    SS_lempinfoid,
    SS_lpostid,
	SS_qualification, //AD20171016
    SS_zpostdesc,
    SS_zshiftdesc,
    SS_zacccodeid,
    SS_zacccodedesc,
    SS_znote,
    SS_nbr_items
} SS;

typedef enum {
    LD_rowtype,
    LD_rec_id,
    LD_aircraftid,
    LD_registration,
    LD_demandid,
    LD_outaptid,
    LD_outicao,
    LD_inaptid,
    LD_inicao,
	LD_outfboid,
	LD_infboid,
    LD_rec_outtime,
    LD_rec_intime,
    LD_manuallyassigned,
    LD_nbr_items
} LEGDATA;

typedef enum {
	PPC_pairconstraintid,
	PPC_crewid1,
	PPC_emp1,
	PPC_crewname1,
	PPC_crewid2,
	PPC_emp2,
	PPC_crewname2,
	PPC_Check_Airman,
	PPC_Standards_Captain,
	PPC_Assistant_Chief_Pilot,
	PPC_Office_Management,
	PPC_Unrestricted,
	PPC_High_Minimums,
	PPC_Restricted,
	PPC_priority,
	PPC_startdate,
	PPC_enddate,
	PPC_nbr_items
} PPC;

typedef struct crewid {
    int crewid;
    char *employeenumber;
    int baseairportid; 
    char *baseicao;
    char *fileas;
} CrewID;

typedef struct crewData {
    char *employeenumber;
    char *aircraftTypeName;
    int aircraftTypeID;
    DateTime starttime;
	DateTime actual_starttime;  //Jintao's change 10/02/2007
    DateTime endtime; 
	DateTime actual_endtime;   //Jintao's change 10/02/2007
    int crewid;
    char *fileas;
    int crewassignmentid;
    int aircraftid;
    char *registration;
    int position;
    int scenarioid;
} CrewData;

typedef struct cs_crew_data {
    int crewid;
    DateTime starttime;
    DateTime endtime;
    int ca_aircraftid;
    int cd_aircraftid;
    char *ca_registration;
    char *cd_registration;
    int position;
    DateTime scheduled_on;
    DateTime scheduled_off;
    DateTime actual_on;
    DateTime actual_off;
    DateTime currdate;
    DateTime lastupdated;
    int scenarioid;
} CS_CrewData;

typedef struct ss_crew_data {
    char *zbadgeid;  // employeenumber
    char *zdeptdesc; // aircraft type
    DateTime dtdate; // in GMT
    char *zlname;
    char *zfname;
    char *zmname;
    int lempid;
    int lempinfoid;
    int lpostid;
	int qualification; //AD20171016
    char *zpostdesc; // Unrestricted, Restricted, Check Airman, Standards Captain, etc
    char *zshiftdesc; // PIC or SIC
    char *zacccodeid; // AOT, OT, DO, DOU
    char *zacccodedesc;
    char *znote;
    BINTREENODE *bwWorkRoot;
	int baseAirportID;
	int crewID;
} SS_CrewData;

typedef struct pre_crew {
    int crewID;
    int position;
    int aircraftTypeID;
    int crewPairID;
    DateTime tourStartTm;
    DateTime tourEndTm;
    double startEarly;
    double stayLate;
    int startLoc;
    int endLoc;
	int baseairportid;
    int availAirportID;
    DateTime cscd_availDT;  // Is equilent to availDT. (kept the historical name here)
    int cscd_canStartLater;
    int cscd_dutyTime;
    int cscd_blockTime;
    //DateTime availDT;
    int canStartLater;
    int dutyTime;
    int blockTime;
    int hasFlown; //was int activityCode;
    int categoryID;
    char *employeenumber;
	//char *fileas;
    DateTime calculatedStartTime;
    DateTime cs_crew_dutyStartTime;
    BINTREENODE *ssCrewDataRoot;
    BINTREENODE *csCrewDataRoot;  //Needs to be commented out
	BINTREENODE *bwCrewAssgRoot;  //Jintao's change 10/01/2007
    BINTREENODE *flownRoot;
	BINTREENODE *activityLegRoot;
	BINTREENODE *csTravelRoot;
    QLIST *positionList;
    QLIST *aircraftTypeList;
    RawAircraftData *firstFlightOfTour;
    RawAircraftData *lastFlightOfTour;
    int availDuringPlanningWindow;
    int newLogic;
	DateTime on1, on2;  //Jintao's change 10/02/2007
	DateTime off1, off2; //Jintao's change 10/02/2007
	RawAircraftData *lastActivityLeg;
	int AC_AirportID_beforePW; //RLZ for crew location updated
	int assignedACID; //RLZ for crew location updated 01/15/2008
	CsTravelData *lastCsTravelLeg;  //Jintao for cs travel data 05/27/2008
	int inTourTransfer;  //flag if crew has flown leg, but is changing to other AC on commercial flight. 
	int dqIndex; //indicator if the crew is dual qualified = index in dqCrewList - DQ - 12/09/2009 ANG
	int nextAcID; //if we want to break CPAC assignment to follow what scheduler did - CPAC Exception - 12/18/2009 ANG
	int firstOutAptID; //store outAptID for first leg after on1 - FATIGUE - 03/02/10 ANG
	DateTime firstLegOutTime; //store outtime for first leg after on1 - FATIGUE - 03/02/10 ANG
	int isDup; //Whether is a duplicate, 1 = XLS+ - 06/07/11 ANG, 2 = CJ4 - 06/13/11 ANG
	int qualification; //AD20171018
} PRE_Crew;

typedef struct pre_crew_pair {
	int aircraftid;
	BINTREENODE *starttimeCrewidRoot;
} PRE_Crew_Pair;

typedef struct crewPairx
{
	int crewPairID;
	int captainID;
	int flightOffID;
	int crewMemA_pos;
	int crewMemA_crewid;
	DateTime crewMemA_starttime;
	DateTime crewMemA_endtime;
	DateTime crewMemA_tourStartTm;
	DateTime crewMemA_tourEndTm;
	int crewMemB_pos;
	int crewMemB_crewid;
	DateTime crewMemB_starttime;
	DateTime crewMemB_endtime;
	DateTime crewMemB_tourStartTm;
	DateTime crewMemB_tourEndTm;
	int assignedAircraftID;
	char *registration;
	DateTime pairStartTm;
	DateTime pairEndTm;
	int isLocked;
	int lockedToPlane;
	int lockedToHome;
	int hasFlown;
	int *aircraftIDPtr;
	int aircraftCount;
	uint64 pairkey;
} CrewPairX;

typedef struct aircraftcrewpairxref {
	int aircraftid;
	//DateTime starttime;
	//DateTime endtime;
    //int crewid;
	int crewid1;
	int crewid2;
	uint64 pairkey;
	int crewPairID;
} AircraftCrewPairXref;

typedef struct leg_data {
    char *rowtype;
    int rec_id;
    int aircraftid;
    char *registration;
    int demandid;
    int outaptid;
    char *outicao;
    int inaptid;
    char *inicao;
	int outfboid;
	int infboid;
    DateTime rec_outtime;
    DateTime rec_intime;
    int manuallyassigned;
} LEG_DATA;

typedef struct charterstats {
	int ownerid;
	int contractid;
	int trips_since_last_charter;
	int charter_count;
	int trip_count;
} CharterStats;

#define Bit_Check_Airman 0x1
#define Bit_Standards_Captain 0x2
#define Bit_Assistant_Chief_Pilot 0x4
#define Bit_Office_Management 0x8
#define Bit_Unrestricted 0x10
#define Bit_High_Minimums 0x20
#define Bit_Restricted 0x40
#define Bit_Stop 0x80
#define setBit(target,bit) (target |= bit)
#define clearBit(target,bit) (target &= ~bit)
#define testBit(target,bit) (target & bit)
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

// incremental revenue definitions
typedef enum {
	KW_False = 0,
	KW_Owner = 5,
	KW_Demo = 13,
	KW_Training = 36,
	KW_MX = 37,
	KW_Company_use = 43,
	KW_Positioning = 46,
	KW_Upgrade_Approved = 48,
	KW_Downgrade_Approved = 49,
	KW_Upgrade_Req_200 = 52,
	KW_Upgrade_Req_Bravo = 53,
	KW_Upgrade_Req_CJ1 = 54,
	KW_Upgrade_Req_Excel = 55,
	KW_Downgrade_Req_90 = 56,
	KW_Downgrade_Req_200 = 57,
	KW_Downgrade_Req_CJ1 = 58,
	KW_Downgrade_Req_Bravo = 59,
	KW_Member_Use = 62,
	KW_FUEL_STOP = 63,
	KW_Static_Demo = 64,
	KW_Charity = 67,
	KW_Customs_stop = 73,
	KW_Delay_Arr_Wx = 75,
	KW_Guaranteed_Upgrade = 76,
	KW_Guaranteed_Downgrade = 77,
	KW_Consecutive_Leg = 79,
	KW_NO_FUEL_STOP = 80,
	KW_Delay_Arr_ATC = 81,
	KW_Delay_Dep_Pax = 84,
	KW_Delay_Dep_Wx = 85,
	KW_Delay_Dep_ATC = 86,
	KW_Delay_Dep_Mx = 87,
	KW_Delay_Dep_FBO = 88,
	KW_Delay_Arr_Pax = 89,
	KW_Delay_Arr_Mx = 90,
	KW_Delay_Arr_FBO = 91,
	KW_Delay_Arr_Crew = 96,
	KW_Delay_Dep_Crew = 97,
	KW_Delay_Arr_Other = 98,
	KW_Delay_Dep_Other = 99,
	KW_Issue_Catering = 100,
	KW_Issue_Ground = 101,
	KW_Issue_FBO = 102,
	KW_Issue_Accounting = 103,
	KW_Issue_Delayed_Flt = 104,
	KW_Issue_Diversion = 105,
	KW_Issue_Mechanical = 106,
	KW_Issue_Weather = 107,
	KW_Issue_Pax_Complain = 108,
	KW_Issue_Charter = 109,
	KW_Issue_Credits_Comp = 110,
	KW_Issue_Booking_Err = 111,
	KW_Issue_MP_Conflict = 112,
	KW_Issue_Customs = 113,
	KW_Issue_Chartr_Avoid = 114,
	KW_Issue_A_C_Cond = 115,
	KW_Issue_Late_Pax = 116,
	KW_Issue_ATC = 117,
	KW_Issue_Late_pos_leg = 118,
	KW_Issue_Scheduling = 121,
	KW_CTR_Sov_Guar = 124,
	KW_Upgrade_Req_Sov = 125,
	KW_Fuel_Stop_Enroute = 126,
	KW_Fuel_Stop_Removed = 127,
	KW_PPP = 128,
	KW_Issue_Schdl_Devia = 129,
	KW_Issue_Tail_Nbr_Chg = 130,
	KW_Issue_Mx_no_Impact = 131,
	KW_Caribbean_Express = 132,
	KW_Issue_Vector_move = 133,
	KW_Peak_Day_Adj = 134,
	KW_Value_Plus = 135,
	KW_Upgrade_Req_CJ3 = 136,
	KW_Downgrade_Req_CJ3 = 137,
	KW_Marketing = 138,
	KW_Downgrade_Req_Excel = 139,
	KW_Ops_Reviewed = 140,
	KW_Chtr_Avd_Time_Accept = 142,
	KW_Chtr_Avd_Time_Denied = 143,
	KW_Chtr_Avd_A_C_Accept = 144,
	KW_Chtr_Avd_A_C_Deny = 145,
	KW_Upgrade_Req_CX = 180, //CX - 12/04/2009 ANG
	KW_Downgrade_Req_Sov = 181, //CX - 12/04/2009 ANG
	KW_Downgrade_Req_CJ4 = 184, //CJ4 - 03/05/12 ANG
	KW_Upgrade_Req_CJ4 = 185, //CJ4 - 03/05/12 ANG
	KW_end_of_list = 255
} KeyWord;

typedef struct kw_text {
	KeyWord kw;
	char *text;
} KW_Text;

typedef struct seqpos_text {
	int seqpos;
	char *text;
} SP_Text;

typedef enum {
	kw_demand, kw_mgdleg, kw_lpgleg, kw_end_of_list = 255
} keyword_rec_type;

typedef struct kwrec {
	keyword_rec_type kwrt;
	KeyWord kw;
}KW_Rec;

typedef enum {
	ud_ownerid, ud_contractid, ud_dmd_outtime, ud_outtime, ud_demandid, ud_otherid, ud_type,
	ud_shortname, ud_legkeywordid, ud_keyword, ud_contract_seqpos, ud_contract_actype,
	ud_contract_actypeid, ud_seqpos, ud_actype, ud_actypeid, ud_outaptid, ud_inaptid, ud_nbrPsgrs,
	ud_end_of_list = 255
} udSqlColumns;


typedef enum {
	rt_contractid, rt_contract_actypeid,
	rt_contract_actype, // name of contract aircraft, just for log
	rt_actypeid,
	rt_actype, // name of aircraft, just for log
	rt_ratio, rt_end_of_list = 255
} rtSqlColumns;

typedef struct ratios {
  int contractid;
  int contract_actypeid;
  int actypeid;
  double ratios[MAX_AC_TYPES];
} Ratios;

typedef struct demandinfo {
  int ownerid;
  int contractid;
  DateTime dmd_outtime;
  DateTime outtime; // if kwtype is demand, this is same as dmd_outtime; if mgdleg, this is schedout; if lpgleg, this is actualout
  int demandid;
  int otherid; // if kwtype is demand, this is managedlegid; if mgdleg, this is managedlegid; if lpgleg, this is logpagelegid
  char *shortname;
  QLIST *kwq;
  int contract_seqpos;
  int contract_actypeid;
  int seqpos;
  int actypeid;
  int smallestAllowableSeqPos;
  int smallestAllowableIssue;
  int largestAllowableSeqPos;
  int inaptid;
  int outaptid;
  int nbrPsgrs;
  int logic;
  double incRev[MAX_AC_TYPES];
  double revPerH; //Revenue Per Hour RLZ 11/01/2010
} DemandInfo;

typedef struct peakday {
	int level_id;
	Date peak_day;
	//char *shoulder_flag; //New Vector - 05/26/10 ANG - Currently not used.
} PeakDay;

typedef struct peakdaycontractrate {
	int contractid;
	int level_id;
	double hourly_rate;
	double flex_hours;
} PeakDayContractRate;

typedef struct contractrate {
	int contractid;
	int actypeid;
	double rate;
} ContractRate;

typedef struct convert {
	int seqpos;
	int actypeid;
	KeyWord upGrade;
	KeyWord downGrade;
	int idxpos;
	int aircraftCd;
} AC_Convert;

typedef enum {
	SEQ_POS = 0,
	AC_TYPE_ID,
	UPGRADE,
	DOWNGRADE,
	IDX_POS,
	AC_CD
} Cnv_Type;

// end incremental revenue definitions

int readInputData(char *host, char *user, char *password, char *database);
int getExclusion(int typeID, int firstID, int secondID, Exclusion **expptr);
int getAircraftTypeAirportExclusions(int aircraftTypeID, int **airportIDList);
void initializeWarningInfo(Warning_error_Entry *warningPtr);
char *escapeQuotes(char *inp, char *out);
void printCrewEndTourList(void);
void printCrewEndTourSummary(void);
void printAcList(void); // 02/28/08 ANG
void initAcSchedLegIndList(void); // 03/05/08 ANG
void getctgncyincev(int actypeidx, int contingencyidx, int dmdnum, double *incentive); //JTO
int checkIfXlsPlus (int acid); //XLS+ - 06/09/11 ANG
int checkIfCj4 (int acid); //CJ4 - 06/13/11 ANG
void printMaintList(void);//fei
int getSunsetTime(int month, int day, int countryID, double longitude); //Airport Curfew - 05/13/11 ANG
int getSunriseTime(int month, int day, int countryID, double longitude); //Airport Curfew - 05/13/11 ANG
int seqPosMapping(int prod_seq_pos); //CJ4 - 03/05/12 ANG

/************************************************************************************************
*	Function	storeFinalCrewPairList					Date last modified:  02/22/10 ANG		*
*	Purpose:	Store Final Crew Pair List into a MySQL table									*		
************************************************************************************************/
void storeFinalCrewPairList (MY_CONNECTION *myconn, int scenarioid) ; //fei


#endif // CSHOPT_INPUT_INC
