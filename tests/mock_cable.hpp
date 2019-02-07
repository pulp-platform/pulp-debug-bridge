
#include "stdio.h"
#include "cables/cable.hpp"

class MockCable : public Cable {
    public:
        MockCable() : Cable(NULL) {}
        bool access(bool, unsigned int, int, char*) {
            printf("send string\n");
            return false; 
        }
};