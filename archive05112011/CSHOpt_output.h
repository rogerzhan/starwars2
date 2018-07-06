#ifndef CSHOPT_OUTPUT_INC
#define CSHOPT_OUTPUT_INC 1
#include "my_mysql.h"
void initializeOutputData(int updateRemote);
void writeOutputData(MY_CONNECTION *conn, int scenarioid);
void runOptimizerStatistics(MY_CONNECTION *conn, int scenarioid); // 02/12/08 ANG
void runOptimizerStatisticsByDay(MY_CONNECTION *conn, int scenarioid); // 12/05/08 ANG
void updateResultsTableforOAG(MY_CONNECTION *conn, int scenarioid);
void writeWarningData(MY_CONNECTION *conn);
void writeDataforSimu(MY_CONNECTION *conn, int scenarioid);
#endif // CSHOPT_OUTPUT_INC
