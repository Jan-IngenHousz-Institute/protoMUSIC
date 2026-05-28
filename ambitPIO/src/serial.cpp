
// Serial routines that can read/write to either or both Serial devices (Serial (USB) and Serial1 (BLE)) based on a setting
// It also maintains and prints a CRC value
// Can use a packet mode that resends

// Jon Zeeff 2016

// WARNING: Serial_Printf() is limited to 200 characters per call
// Zero is used as a string terminator - it can't be sent. Nor can ETX.

// Should change this to C++ and a class

// Remove BLE related now, may add later  HJC 2022

#include <Arduino.h>
#include <string.h>

int Serial_Peek()
{

  if (Serial && Serial.available())
    return Serial.peek();

  return -1;
}


char *Serial_Input_Chars(char *string, const char *terminators, long unsigned int timeout, unsigned int max_length)  {       // terminating characters must be single characters or a list of single characters (so "+" will terminal on +, while "+-" will terminal on either + or -).
  unsigned long start = millis();
  unsigned count = 0;

  // loop forever or until a condition is met
  for (;;) {
    unsigned now = millis();

    if (timeout > 0 && (now - start) > timeout)       // timeout
      break;                                          // done

    int c = Serial_Peek();
    if (c == -1) {
      //sleep_cpu();
      //sleep_cpu_2();                         // save power
      continue;                            // nothing available
    }

    if (c > 127) break;

    char b = Serial.read();

    if (strchr(terminators, b))  {
      //test_char = b;
      break;

    }                      // terminator char seen - throw it away //AND END THE LOOP!

    if (strchr("\r\n", b))  {  // remove characetrs that can break a JSON.  This occurs AFTER the detection of terminators
      //test_char = b;
      continue;

    }                      // terminator char seen - throw it away //AND END THE LOOP!

    start = now;                                      // restart interval

    string[count] = b;                              // add to string
    count++;
    if (max_length > 0 && count >= max_length){        // too long
      //Serial_Print("JSON too long");
      string[max_length - 1] = 0;
      return string;
    }
  } // for ever

  string[count] = 0;   // always null terminate the string

  return string;

}  // Serial_Input_Chars()


// read a double value (see above)
// empty string returns NAN

double Serial_Input_Double(const char *terminators, long unsigned int timeout) {
  char S[25];
  Serial_Input_Chars(S, terminators, timeout, sizeof(S) - 1);
  if (strlen(S) == 0)
    return NAN;
  return strtod(S, 0);
}  // Serial_Input_Double()


// read a long value (see above)
// invalid strings return 0

long Serial_Input_Long(const char *terminators, long unsigned int timeout) {
  char S[25];
  Serial_Input_Chars(S, terminators, timeout, sizeof(S) - 1);
  return atol(S);
}  // Serial_Input_Long()


// Caution: only good for short strings

String Serial_Input_String(const char *terminators, long unsigned int timeout)
{
  static String serial_string;
  char str[100 + 1]; // caution - fixed size buffer

  Serial_Input_Chars(str, terminators, timeout, sizeof(str) - 1);

  serial_string = str;

  //  assert(strlen(str) < 100);

  return serial_string;
}  // user_enter_str()
