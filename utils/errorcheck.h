#include <iostream>

int errorcheck(int i, const char * message){
    
    // if return value was -1
    if (i ==-1){

        // print error message and exit
        std::cerr << "Error Encountered with: " << message << std::endl;
        std::cerr << "Errno: " << errno << std::endl;
        _exit(1);
    }

    // else return the return value 
    return i;
}