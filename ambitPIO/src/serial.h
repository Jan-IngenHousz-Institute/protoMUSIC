

int Serial_Peek();
char *Serial_Input_Chars(char *string, const char *terminators, long unsigned int timeout, unsigned int max_length);
double Serial_Input_Double(const char *terminators, long unsigned int timeout);
long Serial_Input_Long(const char *terminators, long unsigned int timeout);

