// Function Declarations
int createDutyNodes(void);
int checkPlaneExclusions(Duty *duty, Aircraft *plane, int day);
int getRepoArriveTm(int startAptID, int endAptID, int earlyDepTm, int elapsedTm);
int getRepoDepartTm(int startAptID, int endAptID, int lateArrTm, int elapsedTm);
