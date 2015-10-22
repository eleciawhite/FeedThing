#ifndef DEBUG_OUTPUT_H
#define DEBUG_OUTPUT_H

#if DEBUG_ON

#define debugInit()   Serial.begin(9600);
#define debugOutput(x) Serial.print(x);
#define debugOutputLn(x) Serial.println(x);

#else

#define debugInit()   
#define debugOutput(x)
#define debugOutputLn(x) 

#endif /* DEBUG_ON */

#endif /* DEBUG_OUTPUT_H */
